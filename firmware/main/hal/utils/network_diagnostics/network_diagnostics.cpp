/* SPDX-License-Identifier: MIT */
#include "network_diagnostics.h"
#include "../secret_logic/secret_logic.h"
#include <hal/hal.h>
#include <board.h>
#include <settings.h>
#include <system_info.h>
#include <assets/lang_config.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

namespace network_diagnostics {
namespace {
constexpr int RequestTimeoutMs    = 8000;
constexpr size_t MaxResponseBytes = 16384;
std::mutex WifiStartupMutex;
// A closed page can leave an SDK call in flight. New pages must not accumulate
// tasks, stacks and HTTP clients while that call is still being cleaned up.
std::atomic_bool DiagnosticTaskActive{false};
bool WifiProvisioningActive = false;
using Headers               = std::vector<std::pair<std::string, std::string>>;

void logResult(const char* service, const ServiceResult& result)
{
    const char* status = "unknown";
    switch (result.status) {
        case Status::Waiting:
            status = "waiting";
            break;
        case Status::Checking:
            status = "checking";
            break;
        case Status::Available:
            status = "available";
            break;
        case Status::Reachable:
            status = "reachable";
            break;
        case Status::Failed:
            status = "failed";
            break;
    }
    // Log only fixed labels and numeric results. Never include URL, headers,
    // request/response bodies or device/account identifiers.
    ESP_LOGI("NetworkTest", "%s result=%s http=%d elapsed_ms=%d", service, status, result.http_status,
             result.elapsed_ms);
}

std::string httpUrl(std::string url)
{
    if (url.compare(0, 5, "ws://") == 0) url.replace(0, 5, "http://");
    if (url.compare(0, 6, "wss://") == 0) url.replace(0, 6, "https://");
    return url;
}

ServiceResult transportFailure(esp_http_client_handle_t client, esp_err_t error)
{
    const auto socket_error = esp_http_client_get_errno(client);
    int tls_error = 0, tls_flags = 0;
    const auto tls_result  = esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &tls_flags);
    const std::string name = esp_err_to_name(tls_result != ESP_OK ? tls_result : error);
    std::string detail;
    if (name.find("RESOLVE") != std::string::npos)
        detail = "DNS lookup failed. Check the router DNS or server address.";
    else if (tls_flags != 0)
        detail = "TLS certificate verification failed. Check the device time and server certificate.";
    else if (error == ESP_ERR_TIMEOUT || socket_error == ETIMEDOUT)
        detail = "Connection timed out. Check Internet access and firewall settings.";
    else if (socket_error == ECONNREFUSED)
        detail = "The server refused the connection.";
    else if (socket_error == ENETUNREACH || socket_error == EHOSTUNREACH)
        detail = "The server network is unreachable.";
    else
        detail = "Connection failed: " + name + (tls_error ? " (TLS " + std::to_string(tls_error) + ")" : "");
    return {Status::Failed, detail};
}

ServiceResult checkResponse(int status, const std::string& body, bool xiaozhi)
{
    if (status == 401 || status == 403) {
        return {Status::Reachable,
                "Server reached; authorization rejected (HTTP " + std::to_string(status) +
                    "). Check activation/account and device time.",
                status};
    }
    if (status < 200 || status >= 300) {
        return {Status::Failed, "Server reached, but service returned HTTP " + std::to_string(status) + ".", status};
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(
        body.find('\0') == std::string::npos ? cJSON_ParseWithOpts(body.c_str(), nullptr, true) : nullptr,
        cJSON_Delete);
    if (!json || !cJSON_IsObject(json.get()))
        return {Status::Failed, "Server returned an invalid response. Check the server address or captive portal.",
                status};
    if (xiaozhi) {
        for (const auto* field : {"firmware", "activation", "server_time", "mqtt", "websocket"}) {
            if (cJSON_IsObject(cJSON_GetObjectItem(json.get(), field)))
                return {Status::Available, "OTA/configuration service connected", status};
        }
        return {Status::Failed, "Xiaozhi configuration response is missing service information.", status};
    }
    const auto* code = cJSON_GetObjectItem(json.get(), "code");
    if (code && (!cJSON_IsNumber(code) || code->valuedouble != static_cast<double>(code->valueint)))
        return {Status::Failed, "Device API returned an invalid business code.", status};
    if (cJSON_IsNumber(code) && code->valueint != 0) {
        return {Status::Reachable,
                "Device API reached; request rejected (code " + std::to_string(code->valueint) +
                    "). Check account/activation and device time.",
                status};
    }
    const auto* data   = cJSON_GetObjectItem(json.get(), "data");
    const auto* device = cJSON_IsObject(data) ? data : json.get();
    const auto* name   = cJSON_GetObjectItem(device, "name");
    if (!cJSON_IsString(name) || !name->valuestring[0])
        return {Status::Failed, "Device API returned an unexpected response.", status};
    return {Status::Available, "Device API connected", status};
}

ServiceResult probe(const std::string& url, const Headers& headers, const std::string& body, bool xiaozhi,
                    const std::atomic_bool& cancelled)
{
    if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
        return {Status::Failed, "The configured server URL is invalid."};
    }
    esp_http_client_config_t config{};
    config.url                   = url.c_str();
    config.method                = xiaozhi ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.timeout_ms            = RequestTimeoutMs;
    config.disable_auto_redirect = true;
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    auto* client = esp_http_client_init(&config);
    if (!client) return {Status::Failed, "Not enough memory to start the network test."};
    const auto started = esp_timer_get_time();
    auto remaining     = [&]() { return RequestTimeoutMs - static_cast<int>((esp_timer_get_time() - started) / 1000); };
    auto result        = [&]() -> ServiceResult {
        for (const auto& header : headers)
            esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
        esp_http_client_set_header(client, "Connection", "close");
        auto error = esp_http_client_open(client, body.size());
        if (error != ESP_OK) return transportFailure(client, error);
        size_t sent = 0;
        while (sent < body.size()) {
            if (cancelled || remaining() <= 0) return {Status::Failed, "Network test cancelled or timed out."};
            esp_http_client_set_timeout_ms(client, std::max(1, remaining()));
            const auto written = esp_http_client_write(client, body.data() + sent, body.size() - sent);
            if (written <= 0) return transportFailure(client, ESP_FAIL);
            sent += written;
        }
        if (cancelled || remaining() <= 0) return {Status::Failed, "Network test cancelled or timed out."};
        esp_http_client_set_timeout_ms(client, std::max(1, remaining()));
        if (esp_http_client_fetch_headers(client) < 0) return transportFailure(client, ESP_FAIL);
        const auto status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) return checkResponse(status, "", xiaozhi);
        std::string response;
        char buffer[512];
        // fetch_headers may already have buffered the complete body. Drain the
        // read cache before consulting the completion flag.
        while (true) {
            if (cancelled || remaining() <= 0) return {Status::Failed, "Response timed out.", status};
            esp_http_client_set_timeout_ms(client, std::max(1, remaining()));
            const auto bytes = esp_http_client_read(client, buffer, sizeof(buffer));
            if (bytes < 0) return transportFailure(client, ESP_FAIL);
            if (bytes == 0) {
                if (!esp_http_client_is_complete_data_received(client))
                    return {Status::Failed, "The server response was interrupted.", status};
                break;
            }
            if (response.size() + bytes > MaxResponseBytes)
                return {Status::Failed, "The server response exceeded the diagnostic limit.", status};
            response.append(buffer, bytes);
        }
        return checkResponse(status, response, xiaozhi);
    }();
    result.elapsed_ms = static_cast<int>((esp_timer_get_time() - started) / 1000);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}
}  // namespace

struct NetworkDiagnostics::State {
    std::mutex mutex;
    Snapshot results;
    std::atomic_bool cancelled{false};
};

NetworkDiagnostics::NetworkDiagnostics() : _state(std::make_shared<State>())
{
}
NetworkDiagnostics::~NetworkDiagnostics()
{
    _state->cancelled = true;
}

void prepareWifiProvisioning()
{
    std::lock_guard<std::mutex> lock(WifiStartupMutex);
    WifiProvisioningActive = true;
    WifiManager::GetInstance().StopStation();
}

void finishWifiProvisioning()
{
    std::lock_guard<std::mutex> lock(WifiStartupMutex);
    WifiProvisioningActive = false;
}

Snapshot NetworkDiagnostics::snapshot() const
{
    std::lock_guard<std::mutex> lock(_state->mutex);
    return _state->results;
}

bool NetworkDiagnostics::start()
{
    if (snapshot().running) return false;
    _state->cancelled = true;
    _state            = std::make_shared<State>();
    bool expected     = false;
    if (!DiagnosticTaskActive.compare_exchange_strong(expected, true)) {
        _state->results.wifi = "Previous network test is still finishing. Try again shortly.";
        _state->results.xiaozhi =
            _state->results.m5stack = {Status::Failed, "Wait for the previous test, then choose Test Again."};
        return false;
    }
    _state->results.running = true;
    auto* argument          = new std::shared_ptr<State>(_state);
    const auto created      = xTaskCreate(
        [](void* opaque) {
            auto* state = static_cast<std::shared_ptr<State>*>(opaque);
            run(*state);
            {
                std::lock_guard<std::mutex> lock((*state)->mutex);
                if ((*state)->cancelled) {
                    ESP_LOGI("NetworkTest", "Test cancelled");
                } else {
                    logResult("Xiaozhi", (*state)->results.xiaozhi);
                    logResult("M5Stack", (*state)->results.m5stack);
                }
                DiagnosticTaskActive      = false;
                (*state)->results.running = false;
            }
            delete state;
            vTaskDelete(nullptr);
        },
        "network_test", 8192, argument, 2, nullptr);
    if (created != pdPASS) {
        delete argument;
        std::lock_guard<std::mutex> lock(_state->mutex);
        DiagnosticTaskActive    = false;
        _state->results.running = false;
        _state->results.wifi    = "Could not start the network test.";
        _state->results.xiaozhi =
            _state->results.m5stack = {Status::Failed, "Not enough memory to create a test task."};
        return false;
    }
    return true;
}

void NetworkDiagnostics::run(const std::shared_ptr<State>& state)
{
    auto& wifi = WifiManager::GetInstance();
    {
        std::lock_guard<std::mutex> lock(WifiStartupMutex);
        if (state->cancelled) return;
        if (WifiProvisioningActive) {
            std::lock_guard<std::mutex> results_lock(state->mutex);
            state->results.wifi = "App Wi-Fi setup is active. Exit Settings to restart, then test again.";
            state->results.xiaozhi =
                state->results.m5stack = {Status::Failed, "Finish Change Wi-Fi before testing servers."};
            return;
        }
        if (!wifi.IsConnected() && !SsidManager::GetInstance().GetSsidList().empty()) {
            // Starting through WifiBoard arms its automatic AP-provisioning
            // timer. A diagnostic must not switch modes after the page closes.
            WifiManagerConfig config;
            config.ssid_prefix = "Xiaozhi";
            config.language    = Lang::CODE;
            if (!wifi.IsInitialized() && !wifi.Initialize(config)) {
                std::lock_guard<std::mutex> results_lock(state->mutex);
                state->results.wifi = "Wi-Fi initialization failed. Restart the device and try again.";
                state->results.xiaozhi =
                    state->results.m5stack = {Status::Failed, "Wi-Fi is unavailable; server was not tested."};
                return;
            }
            wifi.StartStation();
        }
    }
    const auto wifi_deadline = esp_timer_get_time() + 15000000;
    while (!wifi.IsConnected() && !state->cancelled && esp_timer_get_time() < wifi_deadline &&
           !SsidManager::GetInstance().GetSsidList().empty()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (state->cancelled) return;
    if (!wifi.IsConnected()) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->results.wifi = "Wi-Fi is not connected. Use Change Wi-Fi, then try again.";
        state->results.xiaozhi =
            state->results.m5stack = {Status::Failed, "No Wi-Fi connection; server was not tested."};
        return;
    }
    wifi_ap_record_t ap{};
    esp_netif_ip_info_t ip{};
    char address[16]{};
    esp_wifi_sta_get_ap_info(&ap);
    if (auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) esp_ip4addr_ntoa(&ip.ip, address, sizeof(address));
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->results.wifi = "Wi-Fi: " +
                              std::string(reinterpret_cast<const char*>(ap.ssid),
                                          strnlen(reinterpret_cast<const char*>(ap.ssid), sizeof(ap.ssid))) +
                              "\nIP: " + address;
        state->results.xiaozhi = {Status::Checking, "Connecting to Xiaozhi..."};
    }
    GetHAL().startSntp();
    const auto clock_deadline = esp_timer_get_time() + 5000000;
    while (time(nullptr) < 1577836800 && !state->cancelled && esp_timer_get_time() < clock_deadline) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (state->cancelled) return;
    Settings settings("wifi", false);
    auto url = settings.GetString("ota_url");
    if (url.empty()) url = CONFIG_OTA_URL;
    auto& board                   = Board::GetInstance();
    const Headers xiaozhi_headers = {{"Activation-Version", "1"},     {"Device-Id", SystemInfo::GetMacAddress()},
                                     {"Client-Id", board.GetUuid()},  {"User-Agent", SystemInfo::GetUserAgent()},
                                     {"Accept-Language", Lang::CODE}, {"Content-Type", "application/json"}};
    auto xiaozhi                  = probe(url, xiaozhi_headers, board.GetSystemInfoJson(), true, state->cancelled);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->results.xiaozhi = std::move(xiaozhi);
        state->results.m5stack = {Status::Checking, "Connecting to M5Stack..."};
    }
    if (state->cancelled) return;
    auto base = httpUrl(secret_logic::get_server_url());
    while (!base.empty() && base.back() == '/') base.pop_back();
    const auto token = secret_logic::generate_auth_token();
    auto m5stack =
        token.empty() ? ServiceResult{Status::Failed, "Could not generate device authorization."}
                      : probe(base + "/stackChan/device/info", {{"Authorization", token}}, "", false, state->cancelled);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->results.m5stack = std::move(m5stack);
    }
}

}  // namespace network_diagnostics
