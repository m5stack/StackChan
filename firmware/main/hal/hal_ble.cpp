/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/bleprph/bleprph.h"
#include "utils/secret_logic/secret_logic.h"
#include <ArduinoJson.hpp>
#include <algorithm>
#include <cstdint>
#include <mooncake_log.h>
#include <mooncake.h>
#include <settings.h>
#include <esp_mac.h>

static const std::string_view _tag              = "HAL-BLE";
static const uint8_t _ble_fragment_magic0       = 0xAA;
static const uint8_t _ble_fragment_magic1       = 0x55;
static const uint8_t _ble_fragment_magic2       = 0xC3;
static const uint8_t _ble_fragment_version      = 1;
static const uint16_t _ble_fragment_header_len  = 10;
static const uint16_t _ble_fallback_payload_len = 20;
static uint16_t _ble_dynamic_payload            = _ble_fallback_payload_len;

using BleNotifyCallback = int (*)(const char*, uint16_t);

static void _recordIncomingWritePayload(uint16_t len)
{
    if (len > _ble_dynamic_payload && len <= STACKCHAN_MAX_JSON_LEN) {
        _ble_dynamic_payload = len;
    }
}

static bool _sendFragmentedNotify(BleNotifyCallback notify, const char* json_data, uint16_t json_len, const char* tag)
{
    const uint16_t usable_payload = _ble_dynamic_payload;

    if (json_data == nullptr || json_len == 0) {
        return true;
    }

    if (json_len > STACKCHAN_MAX_JSON_LEN) {
        mclog::tagWarn(_tag, "{} payload exceed max len: {}", tag, json_len);
        return false;
    }

    if (json_len <= usable_payload) {
        return notify(json_data, json_len) == 0;
    }

    if (usable_payload <= _ble_fragment_header_len) {
        mclog::tagWarn(_tag, "{} mtu payload too small for fragmentation", tag);
        return false;
    }

    const uint16_t max_chunk_payload = usable_payload - _ble_fragment_header_len;
    const uint16_t total_packets     = (json_len + max_chunk_payload - 1) / max_chunk_payload;

    if (total_packets == 0) {
        mclog::tagWarn(_tag, "{} invalid packet count", tag);
        return false;
    }

    mclog::tagInfo(_tag, "{} fragmented notify: total={}, mtu_payload={}, chunk_payload={}, packets={}", tag, json_len,
                   usable_payload, max_chunk_payload, total_packets);

    for (uint16_t idx = 0; idx < total_packets; idx++) {
        const uint16_t start     = idx * max_chunk_payload;
        const uint16_t chunk_len = std::min<uint16_t>(max_chunk_payload, json_len - start);

        std::string packet;
        packet.reserve(_ble_fragment_header_len + chunk_len);
        packet.push_back(static_cast<char>(_ble_fragment_magic0));
        packet.push_back(static_cast<char>(_ble_fragment_magic1));
        packet.push_back(static_cast<char>(_ble_fragment_magic2));
        packet.push_back(static_cast<char>(_ble_fragment_version));

        packet.push_back(static_cast<char>((idx >> 8) & 0xFF));
        packet.push_back(static_cast<char>(idx & 0xFF));
        packet.push_back(static_cast<char>((total_packets >> 8) & 0xFF));
        packet.push_back(static_cast<char>(total_packets & 0xFF));
        packet.push_back(static_cast<char>((json_len >> 8) & 0xFF));
        packet.push_back(static_cast<char>(json_len & 0xFF));

        packet.append(json_data + start, json_data + start + chunk_len);

        if (notify(packet.data(), static_cast<uint16_t>(packet.size())) != 0) {
            mclog::tagWarn(_tag, "{} fragmented notify failed at packet={}", tag, idx);
            return false;
        }
    }

    return true;
}

class BleFragmentAssembler {
public:
    bool consume(const char* data, uint16_t len, std::string& out_data, uint16_t& out_len)
    {
        if (!isFragmentFrame(data, len)) {
            out_data.assign(data, data + len);
            reset();
            out_len = len;
            return true;
        }

        const auto* raw              = reinterpret_cast<const uint8_t*>(data);
        const uint16_t packet_index  = (uint16_t(raw[4]) << 8) | uint16_t(raw[5]);
        const uint16_t total_packets = (uint16_t(raw[6]) << 8) | uint16_t(raw[7]);
        const uint16_t total_len     = (uint16_t(raw[8]) << 8) | uint16_t(raw[9]);
        const uint16_t payload_len   = len - _ble_fragment_header_len;

        if (total_packets == 0 || total_len == 0 || total_len > STACKCHAN_MAX_JSON_LEN) {
            mclog::tagWarn(_tag, "ignore invalid fragment header: total_packets={}, total_len={}", total_packets,
                           total_len);
            reset();
            return false;
        }

        if (!_assembling) {
            if (packet_index != 0) {
                mclog::tagWarn(_tag, "unexpected fragment index {}", packet_index);
                return false;
            }
            start(total_len, total_packets);
        } else if (packet_index == 0) {
            // Allow the sender to resend the first packet and restart assembly.
            start(total_len, total_packets);
        } else if (packet_index != _expected_packet_index || total_packets != _expected_total_packets ||
                   total_len != _expected_total_len) {
            mclog::tagWarn(_tag, "fragment sequence mismatch: idx={} expected={}", packet_index,
                           _expected_packet_index);
            reset();
            return false;
        }

        if (payload_len == 0) {
            if (_expected_total_packets > 1) {
                mclog::tagWarn(_tag, "empty fragment payload, total_packets={}", total_packets);
                reset();
                return false;
            }
            out_data = "";
            out_len  = 0;
            reset();
            return true;
        }

        if (_buffer.size() + payload_len > _expected_total_len) {
            mclog::tagWarn(_tag, "fragment payload overflow: idx={} expected_len={} current_len={} frag_len={}",
                           packet_index, _expected_total_len, _buffer.size(), payload_len);
            reset();
            return false;
        }

        _buffer.append(reinterpret_cast<const char*>(raw + _ble_fragment_header_len), payload_len);
        _expected_packet_index++;

        if (_expected_packet_index < _expected_total_packets) {
            if (_buffer.size() < _expected_total_len) {
                return false;
            }
            mclog::tagWarn(_tag, "fragment frame too short before completion: expected_len={} received={}",
                           _expected_total_len, _buffer.size());
            reset();
            return false;
        }

        if (_buffer.size() != _expected_total_len) {
            mclog::tagWarn(_tag, "fragmented frame incomplete: expected={}, received={}", _expected_total_len,
                           _buffer.size());
            reset();
            return false;
        }

        _buffer.push_back('\0');
        out_data = _buffer.substr(0, _expected_total_len);
        out_len  = _expected_total_len;
        reset();
        return true;
    }

private:
    bool isFragmentFrame(const char* data, uint16_t len) const
    {
        if (len <= _ble_fragment_header_len || data == nullptr) {
            return false;
        }

        const auto* raw = reinterpret_cast<const uint8_t*>(data);
        return raw[0] == _ble_fragment_magic0 && raw[1] == _ble_fragment_magic1 && raw[2] == _ble_fragment_magic2 &&
               raw[3] == _ble_fragment_version;
    }

    void start(uint16_t total_len, uint16_t total_packets)
    {
        _assembling             = true;
        _expected_total_len     = total_len;
        _expected_total_packets = total_packets;
        _expected_packet_index  = 0;
        _buffer.clear();
        _buffer.reserve(total_len);
    }

    void reset()
    {
        _assembling             = false;
        _expected_total_len     = 0;
        _expected_total_packets = 0;
        _expected_packet_index  = 0;
        _buffer.clear();
    }

    bool _assembling                 = false;
    uint16_t _expected_total_len     = 0;
    uint16_t _expected_total_packets = 0;
    uint16_t _expected_packet_index  = 0;
    std::string _buffer;
};

static BleFragmentAssembler _motion_assembler;
static BleFragmentAssembler _avatar_assembler;
static BleFragmentAssembler _config_assembler;
static BleFragmentAssembler _rgb_assembler;
static std::string _motion_payload_cache;
static std::string _avatar_payload_cache;
static std::string _config_payload_cache;
static std::string _rgb_payload_cache;

static const char* _cachePayload(std::string& cache, const std::string& payload)
{
    cache = payload;
    return cache.c_str();
}

static int _handle_ble_motion_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    std::string payload;
    uint16_t payload_len = len;
    (void)conn_handle;
    _recordIncomingWritePayload(len);
    if (!_motion_assembler.consume(json_data, len, payload, payload_len)) {
        return 0;
    }
    if (payload_len == 0) {
        return 0;
    }
    GetHAL().onBleMotionData.emit(_cachePayload(_motion_payload_cache, payload));
    return 0;
}

static int _handle_ble_avatar_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    std::string payload;
    uint16_t payload_len = len;
    (void)conn_handle;
    _recordIncomingWritePayload(len);
    if (!_avatar_assembler.consume(json_data, len, payload, payload_len)) {
        return 0;
    }
    if (payload_len == 0) {
        return 0;
    }
    GetHAL().onBleAvatarData.emit(_cachePayload(_avatar_payload_cache, payload));
    return 0;
}

static int _handle_ble_config_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    std::string payload;
    uint16_t payload_len = len;
    (void)conn_handle;
    _recordIncomingWritePayload(len);
    if (!_config_assembler.consume(json_data, len, payload, payload_len)) {
        return 0;
    }
    if (payload_len == 0) {
        return 0;
    }
    GetHAL().onBleConfigData.emit(_cachePayload(_config_payload_cache, payload));
    return 0;
}

static int _handle_ble_rgb_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    std::string payload;
    uint16_t payload_len = len;
    (void)conn_handle;
    _recordIncomingWritePayload(len);
    if (!_rgb_assembler.consume(json_data, len, payload, payload_len)) {
        return 0;
    }
    if (payload_len == 0) {
        return 0;
    }
    GetHAL().onBleRgbData.emit(_cachePayload(_rgb_payload_cache, payload));
    return 0;
}

static uint8_t _handle_ble_battery_read(void)
{
    mclog::tagInfo(_tag, "on bat read");
    return 96;
}

void Hal::ble_init(bool useAltUuid)
{
    mclog::tagInfo(_tag, "init");

    static stackchan_ble_callbacks_t ble_callbacks = {
        .motion_cb       = _handle_ble_motion_write,
        .avatar_cb       = _handle_ble_avatar_write,
        .config_cb       = _handle_ble_config_write,
        .rgb_cb          = _handle_ble_rgb_write,
        .battery_read_cb = _handle_ble_battery_read,
    };
    stackchan_ble_register_callbacks(&ble_callbacks);

    ble_prph_init(useAltUuid);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    mclog::tagInfo(_tag, "init done, factory mac: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", mac[0], mac[1], mac[2],
                   mac[3], mac[4], mac[5]);
}

void Hal::startBleServer()
{
    mclog::tagInfo(_tag, "start ble server");
    ble_init(false);
}

bool Hal::isBleConnected()
{
    return stackchan_ble_is_connected();
}

/* -------------------------------------------------------------------------- */
/*                              App config server                             */
/* -------------------------------------------------------------------------- */
#include "utils/wifi_connect/wifi_station.h"
#include <string_view>
#include <queue>
#include <mutex>
#include <atomic>

class WifiConfigServer {
public:
    void init()
    {
        GetHAL().onBleConfigData.connect([this](const char* data) { on_config_data(data); });
        _was_connected = stackchan_ble_is_connected();

        // Setup WifiStation callbacks
        _wifi_station = std::make_unique<StackChanWifiStation>();
        _wifi_station->OnConnect([this](const std::string& ssid) {
            mclog::tagInfo(_tag, "wifi Connecting to {}", ssid);
            _is_wifi_connecting = true;
            notify_state(0, "wifiConnecting");
        });
        _wifi_station->OnConnected([this](const std::string& ssid) {
            mclog::tagInfo(_tag, "wifi Connected to {}", ssid);
            _is_wifi_connecting = false;
            notify_state(1, "wifiConnected");
            GetHAL().onAppConfigEvent.emit(AppConfigEvent::WifiConnected);

            Settings settings("app_config", true);
            settings.SetBool("is_configed", true);
        });
        _wifi_station->OnConnectFailed([this](const std::string& ssid) {
            mclog::tagInfo(_tag, "wifi Connect Failed to {}", ssid);
            _is_wifi_connecting = false;
            notify_state(2, "wifiConnectFailed");
            GetHAL().onAppConfigEvent.emit(AppConfigEvent::WifiConnectFailed);
        });

        _wifi_station->Start();
    }

    void update()
    {
        bool is_connected = stackchan_ble_is_connected();
        if (is_connected != _was_connected) {
            _was_connected = is_connected;
            if (is_connected) {
                mclog::tagInfo("WifiConfigServer", "app Connected");
                GetHAL().onAppConfigEvent.emit(AppConfigEvent::AppConnected);
            } else {
                mclog::tagInfo("WifiConfigServer", "app Disconnected");
                GetHAL().onAppConfigEvent.emit(AppConfigEvent::AppDisconnected);
            }
        }

        std::string data;
        bool has_data = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_msg_queue.empty()) {
                data = _msg_queue.front();
                _msg_queue.pop();
                has_data = true;
            }
        }

        if (has_data) {
            process_config_data(data.c_str());
        }
    }

private:
    static constexpr std::string_view _tag = "WifiConfigServer";
    std::queue<std::string> _msg_queue;
    std::mutex _mutex;
    bool _was_connected = false;
    std::atomic<bool> _is_wifi_connecting{false};
    std::unique_ptr<StackChanWifiStation> _wifi_station;

    void on_config_data(const char* json_data)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _msg_queue.push(json_data);
    }

    void process_config_data(const char* json_data)
    {
        ArduinoJson::JsonDocument doc;
        auto error = ArduinoJson::deserializeJson(doc, json_data);

        if (error) {
            mclog::tagError(_tag, "deserializeJson() failed: {}", error.c_str());
            return;
        }

        if (doc["cmd"] == "setWifi") {
            handle_set_wifi(doc["data"]);
        } else if (doc["cmd"] == "getWifiStatus") {
            handle_get_wifi_status();
        } else if (doc["cmd"] == "handshake") {
            std::string data = doc["data"].as<std::string>();
            handle_handshake(data);
        }
    }

    void handle_get_wifi_status()
    {
        if (_wifi_station->IsConnected()) {
            notify_state(1, "wifiConnected");
        } else if (_is_wifi_connecting) {
            notify_state(0, "wifiConnecting");
        } else {
            notify_state(3, "wifiDisconnected");
        }
    }

    void handle_set_wifi(ArduinoJson::JsonObject data)
    {
        if (_is_wifi_connecting) {
            mclog::tagWarn(_tag, "busy connecting, ignoring setWifi");
            notify_state(2, "wifiConnectFailed: Busy");
            return;
        }

        const char* ssid     = data["ssid"];
        const char* password = data["password"];

        if (ssid == nullptr || password == nullptr) {
            mclog::tagWarn(_tag, "setWifi is missing ssid or password");
            notify_state(2, "wifiConnectFailed");
            return;
        }

        mclog::tagInfo(_tag, "get wifi config for ssid: {}", ssid);

        // Notify state: connecting
        notify_state(0, "wifiConnecting");
        GetHAL().onAppConfigEvent.emit(AppConfigEvent::TryWifiConnect);

        connect_wifi(ssid, password);
    }

    void handle_handshake(std::string_view data)
    {
        auto token = secret_logic::generate_handshake_token(data);
        notify_state(4, token.c_str());
    }

    void connect_wifi(const char* ssid, const char* password)
    {
        // Save to NVS (compatible with Xiaozhi) and connect
        _wifi_station->AddAuth(ssid, password);
    }

    void notify_state(int type, const char* state)
    {
        ArduinoJson::JsonDocument doc;
        doc["cmd"]           = "notifyState";
        doc["data"]["type"]  = type;
        doc["data"]["state"] = state;

        std::string json_str;
        ArduinoJson::serializeJson(doc, json_str);
        if (json_str.length() > UINT16_MAX) {
            mclog::tagWarn(_tag, "Config notify payload too large: {}", json_str.length());
            return;
        }

        const auto notify_ok = _sendFragmentedNotify(stackchan_ble_notify_config, json_str.c_str(),
                                                     static_cast<uint16_t>(json_str.length()), "Config notify");
        if (!notify_ok) {
            mclog::tagWarn(_tag, "Config notify fragmented send failed");
        }
    }
};

class AppConfigServerWorker : public mooncake::BasicAbility {
public:
    void onCreate() override
    {
        _server = std::make_unique<WifiConfigServer>();
        _server->init();
    }

    void onRunning() override
    {
        if (GetHAL().millis() - _last_tick < 50) {
            return;
        }
        _last_tick = GetHAL().millis();
        _server->update();
    }

    void onDestroy() override
    {
        _server.reset();
    }

private:
    std::unique_ptr<WifiConfigServer> _server;
    uint32_t _last_tick = 0;
};

void Hal::startAppConfigServer()
{
    mclog::tagInfo(_tag, "start app config server");

    ble_init(true);

    mooncake::GetMooncake().extensionManager()->createAbility(std::make_unique<AppConfigServerWorker>());
}

bool Hal::isAppConfiged()
{
    Settings settings("app_config", false);
    return settings.GetBool("is_configed", false);
}

void Hal::resetAppConfiged()
{
    Settings settings("app_config", true);
    settings.SetBool("is_configed", false);
}
