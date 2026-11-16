/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "local_avatar_client.h"
#include "local_network_protocol.h"
#include <board.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include "avatar_socket.h"
#include <wifi_manager.h>

namespace local_network {
namespace {
constexpr const char* Tag = "LAN-Avatar";
// The App announces its endpoint over the remote channel and the device retries
// it a couple of seconds after the previous attempt, so a link that dropped
// mid-session comes back without user interaction.
constexpr uint32_t RetryInterval = 2000;

uint32_t millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

/// The App and this device must share one IPv4 subnet, so every attempt logs the
/// joined SSID and address.
std::string activeNetwork()
{
    const auto& wifi = WifiManager::GetInstance();
    const auto ssid  = wifi.GetSsid();
    const auto ip    = wifi.GetIpAddress();
    return "ssid '" + (ssid.empty() ? std::string("<unknown>") : ssid) + "' ip " +
           (ip.empty() ? std::string("<none>") : ip);
}
}  // namespace

AvatarClient::AvatarClient(std::string factory_mac, TokenGenerator token_generator, Receiver receiver)
    : _factory_mac(normalizeMac(factory_mac)),
      _token_generator(std::move(token_generator)),
      _receiver(std::move(receiver))
{
}

AvatarClient::~AvatarClient()
{
    _stopping  = true;
    _connected = false;
    if (_task != nullptr) {
        xTaskNotifyGive(_task);
        // Let the task finish its queries/connection attempt and close the socket
        // before destroying callback targets or mutexes.
        xSemaphoreTake(_stopped, portMAX_DELAY);
    }
    if (_stopped != nullptr) vSemaphoreDelete(_stopped);
}

bool AvatarClient::start()
{
    if (_task != nullptr) return true;
    if (_factory_mac.empty() || _stopping) return false;
    _stopped = xSemaphoreCreateBinary();
    if (_stopped == nullptr) return false;
    const auto result = xTaskCreate(
        [](void* argument) {
            auto* client = static_cast<AvatarClient*>(argument);
            client->run();
            xSemaphoreGive(client->_stopped);
            vTaskDelete(nullptr);
        },
        "avatar_lan", 8192, this, 2, &_task);
    if (result != pdPASS) {
        vSemaphoreDelete(_stopped);
        _stopped = nullptr;
        _task    = nullptr;
        ESP_LOGE(Tag, "Failed to start LAN connection task");
        return false;
    }
    ESP_LOGI(Tag, "LAN client started for factory MAC %s; waiting for the App endpoint", _factory_mac.c_str());
    return true;
}

bool AvatarClient::isConnected() const
{
    return _connected && !_stopping;
}

uint32_t AvatarClient::generation() const
{
    return _generation;
}

void AvatarClient::run()
{
    bool first_attempt    = true;
    uint32_t last_attempt = 0;
    uint32_t attempt      = 0;
    while (!_stopping) {
        if (!WifiManager::GetInstance().IsConnected()) {
            disconnect();
            first_attempt = true;
        } else if (!isConnected()) {
            disconnect();
            const auto revision  = _endpoint_revision.load();
            const bool announced = revision != _applied_revision;
            if (announced || first_attempt || millis() - last_attempt >= RetryInterval) {
                first_attempt     = false;
                _applied_revision = revision;
                if (hasAnnouncedEndpoint()) {
                    ESP_LOGI(Tag, "LAN attempt #%u on %s: connecting to the App announced endpoint",
                             static_cast<unsigned>(++attempt), activeNetwork().c_str());
                    if (!connectAnnouncedEndpoint()) {
                        ESP_LOGI(Tag, "App announced endpoint unreachable; remote transport remains in use");
                    }
                } else {
                    ESP_LOGI(Tag, "LAN attempt #%u on %s: waiting for the App to announce its endpoint",
                             static_cast<unsigned>(++attempt), activeNetwork().c_str());
                }
                last_attempt = millis();
            }
        }
        // A connected App sends WebSocket ping every five seconds. The network
        // component replies pong itself; a missed notification must not tear the
        // link down.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    }
    disconnect();
}

void AvatarClient::setEndpoint(std::vector<std::string> hosts, uint16_t port)
{
    {
        std::lock_guard<std::mutex> lock(_endpoint_mutex);
        _endpoint_hosts = std::move(hosts);
        _endpoint_port  = port;
    }
    ++_endpoint_revision;
    if (_task != nullptr) xTaskNotifyGive(_task);
}

bool AvatarClient::hasAnnouncedEndpoint() const
{
    std::lock_guard<std::mutex> lock(_endpoint_mutex);
    return _endpoint_port != 0 && !_endpoint_hosts.empty();
}

/// Connect to every announced address in order; iOS can report a link-local
/// AWDL address besides the Wi-Fi one, so the first reachable address wins.
bool AvatarClient::connectAnnouncedEndpoint()
{
    std::vector<std::string> hosts;
    uint16_t port = 0;
    {
        std::lock_guard<std::mutex> lock(_endpoint_mutex);
        hosts = _endpoint_hosts;
        port  = _endpoint_port;
    }
    if (port == 0) return false;
    for (const auto& host : hosts) {
        if (_stopping) return false;
        if (host.empty()) continue;
        if (connect(host, port)) return true;
    }
    return false;
}

bool AvatarClient::connect(const std::string& host, uint16_t port)
{
    auto token = _token_generator();
    if (_stopping || token.empty()) {
        ESP_LOGW(Tag, "Cannot connect to App: LAN handshake token unavailable");
        return false;
    }
    // The remote Avatar connection uses id 1. Keep a distinct id for networks
    // whose underlying implementation identifies connections by this value.
    auto socket = std::make_shared<AvatarSocket>(*Board::GetInstance().GetNetwork(), 2);
    uint32_t current_generation;
    {
        std::lock_guard<std::mutex> lock(_socket_mutex);
        current_generation = ++_generation;
    }
    socket->SetHeader("Authorization", token.c_str());
    socket->OnDisconnected([this, current_generation]() { markDisconnected(current_generation); });
    socket->OnError([this, current_generation](int) { markDisconnected(current_generation); });
    socket->OnData([this, current_generation](const char* data, size_t len, bool binary) {
        if (!binary || _stopping || current_generation != generation()) return;
        std::vector<uint8_t> frame;
        if (decodeToRemoteFrame(reinterpret_cast<const uint8_t*>(data), len, _factory_mac, frame)) {
            _receiver(current_generation, std::move(frame));
        }
    });
    const auto url = "ws://" + host + ":" + std::to_string(port) + "/ws";
    if (!socket->Connect(url.c_str()) || _stopping) {
        markDisconnected(current_generation);
        ESP_LOGW(Tag, "App WebSocket connection failed at %s; remote transport remains available", url.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_socket_mutex);
        if (_stopping || current_generation != generation() || !socket->IsConnected()) return false;
        _session   = (static_cast<uint64_t>(esp_random() & 0x7fffffff) << 32) | esp_random();
        _sequence  = 0;
        _socket    = socket;
        _connected = socket->IsConnected();
    }
    ESP_LOGI(Tag, "Connected to App at %s:%u", host.c_str(), static_cast<unsigned>(port));
    return isConnected();
}

void AvatarClient::markDisconnected(uint32_t current_generation)
{
    {
        std::lock_guard<std::mutex> lock(_socket_mutex);
        if (current_generation != generation()) return;
        _connected = false;
        ++_generation;  // Reject already queued controls as soon as the link fails.
    }
    if (_task != nullptr) xTaskNotifyGive(_task);
}

void AvatarClient::disconnect()
{
    // Losing the direct link never stops the task and never ends the AVATAR
    // session: senders fall back to the remote channel and the next round
    // retries the last announced endpoint.
    std::shared_ptr<AvatarSocket> old;
    {
        std::lock_guard<std::mutex> lock(_socket_mutex);
        _connected = false;
        if (!_socket) return;
        ++_generation;
        old = std::move(_socket);
    }
    // Socket destruction may wait for the receive task and its callbacks.
    // Never hold a mutex those callbacks or the sender might need here.
}

bool AvatarClient::send(uint8_t type, const uint8_t* data, size_t len)
{
    std::lock_guard<std::mutex> sending(_send_mutex);
    std::shared_ptr<AvatarSocket> socket;
    std::vector<uint8_t> frame;
    uint32_t current_generation;
    {
        std::lock_guard<std::mutex> lock(_socket_mutex);
        if (!isConnected() || !_socket || !encode(type, data, len, _session, ++_sequence, frame)) return false;
        socket             = _socket;
        current_generation = generation();
    }
    if (socket->SendBinary(frame.data(), frame.size())) return true;
    markDisconnected(current_generation);
    return false;
}

}  // namespace local_network
