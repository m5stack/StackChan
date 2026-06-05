#include "wifi_board.h"

#include <esp_log.h>
#include <esp_network.h>
#include <font_awesome.h>
#include <freertos/task.h>
#include <utility>
#include <wifi_manager.h>
#include <wifi_station.h>
#include <ssid_manager.h>

#include "afsk_demod.h"
#include "audio_codec.h"
#include "application.h"
#include "assets/lang_config.h"
#include "display.h"
#include "settings.h"
#include "system_info.h"

#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
#include "blufi.h"
#endif

namespace {

constexpr char kTag[] = "WifiBoard";
constexpr int kConnectTimeoutSec = 60;

struct JsonDeleter {
    void operator()(cJSON* item) const
    {
        if (item != nullptr) {
            cJSON_Delete(item);
        }
    }
};

using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

JsonPtr CreateObject()
{
    JsonPtr object(cJSON_CreateObject());
    if (object == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate JSON object");
    }
    return object;
}

std::string PrintJson(cJSON* root)
{
    char* raw = cJSON_PrintUnformatted(root);
    if (raw == nullptr) {
        ESP_LOGE(kTag, "Failed to serialize JSON");
        return "{}";
    }
    std::string result(raw);
    cJSON_free(raw);
    return result;
}

}  // namespace

WifiBoard::WifiBoard()
{
    const esp_timer_create_args_t timer_args = {
        .callback = OnWifiConnectTimeout,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_connect_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &connect_timer_));
}

WifiBoard::~WifiBoard()
{
    if (connect_timer_ == nullptr) {
        return;
    }
    esp_timer_stop(connect_timer_);
    esp_timer_delete(connect_timer_);
}

std::string WifiBoard::GetBoardType()
{
    return "wifi";
}

void WifiBoard::StartNetwork()
{
    auto& wifi_manager = WifiManager::GetInstance();

    WifiManagerConfig config;
    config.ssid_prefix = "StackChan";
    config.language = Lang::CODE;
    wifi_manager.Initialize(config);

    wifi_manager.SetEventCallback([this](WifiEvent event, const std::string& data) {
        switch (event) {
            case WifiEvent::Scanning:
                OnNetworkEvent(NetworkEvent::Scanning);
                break;
            case WifiEvent::Connecting:
                OnNetworkEvent(NetworkEvent::Connecting, data);
                break;
            case WifiEvent::Connected:
                OnNetworkEvent(NetworkEvent::Connected, data);
                break;
            case WifiEvent::Disconnected:
                OnNetworkEvent(NetworkEvent::Disconnected);
                break;
            case WifiEvent::ConfigModeEnter:
                OnNetworkEvent(NetworkEvent::WifiConfigModeEnter);
                break;
            case WifiEvent::ConfigModeExit:
                OnNetworkEvent(NetworkEvent::WifiConfigModeExit);
                break;
        }
    });

    TryWifiConnect();
}

void WifiBoard::TryWifiConnect()
{
    if (!SsidManager::GetInstance().GetSsidList().empty()) {
        ESP_LOGI(kTag, "Starting WiFi connection attempt");
        ESP_ERROR_CHECK(esp_timer_start_once(connect_timer_, kConnectTimeoutSec * 1000000ULL));
        WifiManager::GetInstance().StartStation();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1500));
    StartWifiConfigMode();
}

void WifiBoard::OnNetworkEvent(NetworkEvent event, const std::string& data)
{
    switch (event) {
        case NetworkEvent::Connected:
            esp_timer_stop(connect_timer_);
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
            Blufi::GetInstance().deinit();
#endif
            in_config_mode_ = false;
            ESP_LOGI(kTag, "Connected to WiFi: %s", data.c_str());
            break;
        case NetworkEvent::Scanning:
            ESP_LOGI(kTag, "WiFi scanning");
            break;
        case NetworkEvent::Connecting:
            ESP_LOGI(kTag, "WiFi connecting to %s", data.c_str());
            break;
        case NetworkEvent::Disconnected:
            ESP_LOGW(kTag, "WiFi disconnected");
            break;
        case NetworkEvent::WifiConfigModeEnter:
            in_config_mode_ = true;
            ESP_LOGI(kTag, "WiFi config mode entered");
            break;
        case NetworkEvent::WifiConfigModeExit:
            in_config_mode_ = false;
            ESP_LOGI(kTag, "WiFi config mode exited");
            TryWifiConnect();
            break;
        default:
            break;
    }

    if (network_event_callback_ != nullptr) {
        network_event_callback_(event, data);
    }
}

void WifiBoard::SetNetworkEventCallback(NetworkEventCallback callback)
{
    network_event_callback_ = std::move(callback);
}

void WifiBoard::OnWifiConnectTimeout(void* arg)
{
    auto* board = static_cast<WifiBoard*>(arg);
    ESP_LOGW(kTag, "WiFi connection timeout, entering config mode");
    WifiManager::GetInstance().StopStation();
    board->StartWifiConfigMode();
}

void WifiBoard::StartWifiConfigMode()
{
    in_config_mode_ = true;
    Application::GetInstance().SetDeviceState(kDeviceStateWifiConfiguring);

#ifdef CONFIG_USE_HOTSPOT_WIFI_PROVISIONING
    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StartConfigAp();

    Application::GetInstance().Schedule([&wifi_manager]() {
        std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
        hint += wifi_manager.GetApSsid();
        hint += Lang::Strings::ACCESS_VIA_BROWSER;
        hint += wifi_manager.GetApWebUrl();
        Application::GetInstance().Alert(
            Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_WIFICONFIG);
    });
#elif CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
    Blufi::GetInstance().init();
#endif

#if CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING
    AudioCodec* codec = Board::GetInstance().GetAudioCodec();
    const int channel = codec != nullptr ? codec->input_channels() : 1;
    ESP_LOGI(kTag, "Starting acoustic WiFi provisioning, channels: %d", channel);

    xTaskCreate(
        [](void* arg) {
            const auto input_channels = reinterpret_cast<intptr_t>(arg);
            auto& app = Application::GetInstance();
            auto& wifi = WifiManager::GetInstance();
            Display* display = Board::GetInstance().GetDisplay();
            audio_wifi_config::ReceiveWifiCredentialsFromAudio(&app, &wifi, display, input_channels);
            vTaskDelete(nullptr);
        },
        "acoustic_wifi", 4096, reinterpret_cast<void*>(channel), 2, nullptr);
#endif
}

void WifiBoard::EnterWifiConfigMode()
{
    ESP_LOGI(kTag, "EnterWifiConfigMode called");
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);

    Application& app = Application::GetInstance();
    const DeviceState state = app.GetDeviceState();
    if (state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateIdle) {
        app.ResetProtocol();

        xTaskCreate(
            [](void* arg) {
                auto* board = static_cast<WifiBoard*>(arg);
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_timer_stop(board->connect_timer_);
                WifiManager::GetInstance().StopStation();
                board->StartWifiConfigMode();
                vTaskDelete(nullptr);
            },
            "wifi_cfg_delay", 4096, this, 2, nullptr);
        return;
    }

    if (state != kDeviceStateStarting) {
        ESP_LOGE(kTag, "EnterWifiConfigMode called in unexpected device state: %d", state);
        return;
    }

    esp_timer_stop(connect_timer_);
    WifiManager::GetInstance().StopStation();
    StartWifiConfigMode();
}

bool WifiBoard::IsInWifiConfigMode() const
{
    return WifiManager::GetInstance().IsConfigMode();
}

NetworkInterface* WifiBoard::GetNetwork()
{
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon()
{
    auto& wifi = WifiManager::GetInstance();
    if (wifi.IsConfigMode()) {
        return FONT_AWESOME_WIFI;
    }
    if (!wifi.IsConnected()) {
        return FONT_AWESOME_WIFI_SLASH;
    }

    const int rssi = wifi.GetRssi();
    if (rssi >= -65) {
        return FONT_AWESOME_WIFI;
    }
    if (rssi >= -75) {
        return FONT_AWESOME_WIFI_FAIR;
    }
    return FONT_AWESOME_WIFI_WEAK;
}

std::string WifiBoard::GetBoardJson()
{
    JsonPtr root = CreateObject();
    if (root == nullptr) {
        return "{}";
    }

    auto& wifi = WifiManager::GetInstance();
    cJSON_AddStringToObject(root.get(), "type", BOARD_TYPE);
    cJSON_AddStringToObject(root.get(), "name", BOARD_NAME);

    if (!wifi.IsConfigMode()) {
        cJSON_AddStringToObject(root.get(), "ssid", wifi.GetSsid().c_str());
        cJSON_AddNumberToObject(root.get(), "rssi", wifi.GetRssi());
        cJSON_AddNumberToObject(root.get(), "channel", wifi.GetChannel());
        cJSON_AddStringToObject(root.get(), "ip", wifi.GetIpAddress().c_str());
    }

    cJSON_AddStringToObject(root.get(), "mac", SystemInfo::GetMacAddress().c_str());
    return PrintJson(root.get());
}

void WifiBoard::SetPowerSaveLevel(PowerSaveLevel level)
{
    WifiPowerSaveLevel wifi_level = WifiPowerSaveLevel::PERFORMANCE;
    switch (level) {
        case PowerSaveLevel::LOW_POWER:
            wifi_level = WifiPowerSaveLevel::LOW_POWER;
            break;
        case PowerSaveLevel::BALANCED:
            wifi_level = WifiPowerSaveLevel::BALANCED;
            break;
        case PowerSaveLevel::PERFORMANCE:
            break;
    }
    WifiManager::GetInstance().SetPowerSaveLevel(wifi_level);
}

std::string WifiBoard::GetDeviceStatusJson()
{
    JsonPtr root = CreateObject();
    if (root == nullptr) {
        return "{}";
    }

    Board& board = Board::GetInstance();

    JsonPtr audio_speaker = CreateObject();
    if (audio_speaker != nullptr) {
        if (AudioCodec* codec = board.GetAudioCodec(); codec != nullptr) {
            cJSON_AddNumberToObject(audio_speaker.get(), "volume", codec->output_volume());
        }
        cJSON_AddItemToObject(root.get(), "audio_speaker", audio_speaker.release());
    }

    JsonPtr screen = CreateObject();
    if (screen != nullptr) {
        if (Backlight* backlight = board.GetBacklight(); backlight != nullptr) {
            cJSON_AddNumberToObject(screen.get(), "brightness", backlight->brightness());
        }
        if (Display* display = board.GetDisplay(); display != nullptr && display->height() > 64) {
            if (auto* theme = display->GetTheme(); theme != nullptr) {
                cJSON_AddStringToObject(screen.get(), "theme", theme->name().c_str());
            }
        }
        cJSON_AddItemToObject(root.get(), "screen", screen.release());
    }

    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        JsonPtr battery = CreateObject();
        if (battery != nullptr) {
            cJSON_AddNumberToObject(battery.get(), "level", battery_level);
            cJSON_AddBoolToObject(battery.get(), "charging", charging);
            cJSON_AddItemToObject(root.get(), "battery", battery.release());
        }
    }

    auto& wifi = WifiManager::GetInstance();
    JsonPtr network = CreateObject();
    if (network != nullptr) {
        cJSON_AddStringToObject(network.get(), "type", "wifi");
        cJSON_AddStringToObject(network.get(), "ssid", wifi.GetSsid().c_str());
        const int rssi = wifi.GetRssi();
        const char* signal = rssi >= -60 ? "strong" : (rssi >= -70 ? "medium" : "weak");
        cJSON_AddStringToObject(network.get(), "signal", signal);
        cJSON_AddItemToObject(root.get(), "network", network.release());
    }

    float chip_temperature = 0.0f;
    if (board.GetTemperature(chip_temperature)) {
        JsonPtr chip = CreateObject();
        if (chip != nullptr) {
            cJSON_AddNumberToObject(chip.get(), "temperature", chip_temperature);
            cJSON_AddItemToObject(root.get(), "chip", chip.release());
        }
    }

    return PrintJson(root.get());
}
