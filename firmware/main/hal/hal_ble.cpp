/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/bleprph/bleprph.h"
#include "utils/secret_logic/secret_logic.h"
#include <ArduinoJson.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <settings.h>
#include <esp_mac.h>

static const std::string_view _tag = "HAL-BLE";

static int _handle_ble_motion_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    // mclog::tagInfo(_tag, "on motion:\n{}", json_data);
    GetHAL().onBleMotionData.emit(json_data);
    return 0;
}

static int _handle_ble_avatar_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    // mclog::tagInfo(_tag, "on avatar:\n{}", json_data);
    GetHAL().onBleAvatarData.emit(json_data);
    return 0;
}

static int _handle_ble_config_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    // mclog::tagInfo(_tag, "on config:\n{}", json_data);
    GetHAL().onBleConfigData.emit(json_data);
    return 0;
}

static int _handle_ble_rgb_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    // mclog::tagInfo(_tag, "on rgb:\n{}", json_data);
    GetHAL().onBleRgbData.emit(json_data);
    return 0;
}

static uint8_t _handle_ble_battery_read(void)
{
    mclog::tagInfo(_tag, "on bat read");
    return 96;
}

void Hal::ble_init()
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

    ble_prph_init();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    mclog::tagInfo(_tag, "init done, factory mac: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", mac[0], mac[1], mac[2],
                   mac[3], mac[4], mac[5]);
}

void Hal::startBleServer()
{
    mclog::tagInfo(_tag, "start ble server");
    ble_init();
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

        mclog::tagInfo(_tag, "get wifi config: {} / {}", ssid, password);

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
        stackchan_ble_notify_config(json_str.c_str(), json_str.length());
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

    ble_init();

    mooncake::GetMooncake().extensionManager()->createAbility(std::make_unique<AppConfigServerWorker>());
}

bool Hal::isAppConfiged()
{
    Settings settings("app_config", false);
    return settings.GetBool("is_configed", false);
}
