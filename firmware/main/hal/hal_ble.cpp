/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/bleprph/bleprph.h"
#include <ArduinoJson.hpp>
#include <mooncake_log.h>
#include <esp_mac.h>
#include <settings.h>

static const std::string _tag = "HAL-BLE";

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
    mclog::tagInfo(_tag, "on config:\n{}", json_data);
    GetHAL().onBleConfigData.emit(json_data);
    return 0;
}

static int _handle_ble_animation_write(const char* json_data, uint16_t len, uint16_t conn_handle)
{
    mclog::tagInfo(_tag, "on animation:\n{}", json_data);
    GetHAL().onBleAnimationData.emit(json_data);
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
        .animation_cb    = _handle_ble_animation_write,
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

class WifiConfigServer {
public:
    void init()
    {
        GetHAL().onBleConfigData.connect([this](const char* data) { on_config_data(data); });
        _was_connected = stackchan_ble_is_connected();

        // Setup WifiStation callbacks
        auto& wifi = StackChanWifiStation::GetInstance();
        wifi.OnConnect([this](const std::string& ssid) {
            mclog::tagInfo(_tag, "Wifi Connecting to {}", ssid);
            notify_state(0, "wifiConnecting");
        });
        wifi.OnConnected([this](const std::string& ssid) {
            mclog::tagInfo(_tag, "Wifi Connected to {}", ssid);
            notify_state(1, "wifiConnected");
            GetHAL().onAppConfigEvent.emit(AppConfigEvent::WifiConnected);

            Settings settings("app_config", true);
            settings.SetBool("is_configed", true);
        });
        wifi.OnConnectFailed([this](const std::string& ssid) {
            mclog::tagInfo(_tag, "Wifi Connect Failed to {}", ssid);
            notify_state(2, "wifiConnectFailed");
            GetHAL().onAppConfigEvent.emit(AppConfigEvent::WifiConnectFailed);
        });

        wifi.Start();
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
        }
    }

    void handle_set_wifi(ArduinoJson::JsonObject data)
    {
        const char* ssid     = data["ssid"];
        const char* password = data["password"];

        mclog::tagInfo("WifiConfigServer", "get wifi config: {} / {}", ssid, password);

        // Notify state: connecting
        notify_state(0, "wifiConnecting");
        GetHAL().onAppConfigEvent.emit(AppConfigEvent::TryWifiConnect);

        connect_wifi(ssid, password);
    }

    void connect_wifi(const char* ssid, const char* password)
    {
        auto& wifi = StackChanWifiStation::GetInstance();

        // Save to NVS (compatible with Xiaozhi) and connect
        wifi.AddAuth(ssid, password);
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

static void _app_config_server_task(void* param)
{
    auto server = std::make_unique<WifiConfigServer>();
    server->init();

    while (1) {
        server->update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Hal::startAppConfigServer()
{
    mclog::tagInfo(_tag, "start app config server");

    ble_init();

    xTaskCreate(_app_config_server_task, "appconfig", 6000, NULL, 10, NULL);
}

bool Hal::isAppConfiged()
{
    Settings settings("app_config", false);
    return settings.GetBool("is_configed", false);
}
