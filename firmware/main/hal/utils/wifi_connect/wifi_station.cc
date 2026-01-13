#include "wifi_station.h"
#include <cstring>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include "ssid_manager.h"

#define TAG                  "StackChanWifiStation"
#define WIFI_EVENT_CONNECTED BIT0
#define MAX_RECONNECT_COUNT  10

StackChanWifiStation::StackChanWifiStation()
{
    event_group_ = xEventGroupCreate();
}

StackChanWifiStation::~StackChanWifiStation()
{
    vEventGroupDelete(event_group_);
}

void StackChanWifiStation::AddAuth(const std::string& ssid, const std::string& password)
{
    // Save to NVS via SsidManager for compatibility
    SsidManager::GetInstance().AddSsid(ssid, password);

    ssid_ = ssid;
    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, ssid.c_str(), ssid.length());
    memcpy(wifi_config.sta.password, password.c_str(), password.length());

    ESP_LOGI(TAG, "Setting WiFi configuration SSID: %s", ssid.c_str());
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    if (on_connect_) {
        on_connect_(ssid_);
    }

    reconnect_count_ = 0;
    is_connecting_   = true;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        ESP_LOGI(TAG, "Already connected, disconnecting first...");
        esp_wifi_disconnect();
        // The reconnection will be handled by WIFI_EVENT_STA_DISCONNECTED
    } else {
        esp_wifi_connect();
    }
}

void StackChanWifiStation::Stop()
{
    if (instance_any_id_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
        instance_got_ip_ = nullptr;
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    station_netif_ = nullptr;
}

void StackChanWifiStation::OnConnect(std::function<void(const std::string& ssid)> on_connect)
{
    on_connect_ = on_connect;
}

void StackChanWifiStation::OnConnected(std::function<void(const std::string& ssid)> on_connected)
{
    on_connected_ = on_connected;
}

void StackChanWifiStation::OnConnectFailed(std::function<void(const std::string& ssid)> on_connect_failed)
{
    on_connect_failed_ = on_connect_failed;
}

void StackChanWifiStation::Start()
{
    if (is_started_) {
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();

    station_netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable         = true;  // Enable NVS to store credentials
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &StackChanWifiStation::WifiEventHandler, this, &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &StackChanWifiStation::IpEventHandler, this, &instance_got_ip_));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    is_started_ = true;
}

bool StackChanWifiStation::WaitForConnected(int timeout_ms)
{
    auto bits =
        xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    return (bits & WIFI_EVENT_CONNECTED) != 0;
}

int8_t StackChanWifiStation::GetRssi()
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

uint8_t StackChanWifiStation::GetChannel()
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.primary;
    }
    return 0;
}

bool StackChanWifiStation::IsConnected()
{
    return xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED;
}

void StackChanWifiStation::SetPowerSaveMode(bool enabled)
{
    esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

void StackChanWifiStation::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    auto* this_ = static_cast<StackChanWifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        // Do not auto connect on start
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);

        // Only retry if we are actively trying to connect
        if (this_->is_connecting_) {
            if (this_->reconnect_count_ < MAX_RECONNECT_COUNT) {
                this_->reconnect_count_++;
                ESP_LOGI(TAG, "Retry to connect to the AP (attempt %d/%d)", this_->reconnect_count_,
                         MAX_RECONNECT_COUNT);
                esp_wifi_connect();
            } else {
                ESP_LOGI(TAG, "Connect to the AP failed");
                this_->is_connecting_ = false;
                if (this_->on_connect_failed_) {
                    this_->on_connect_failed_(this_->ssid_);
                }
            }
        }
    }
}

void StackChanWifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    auto* this_ = static_cast<StackChanWifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Got IP: %s", this_->ip_address_.c_str());

    this_->reconnect_count_ = 0;
    this_->is_connecting_   = false;
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);

    // Update SSID in case we connected from NVS auto-connect
    wifi_config_t conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        this_->ssid_ = (char*)conf.sta.ssid;
    }

    if (this_->on_connected_) {
        this_->on_connected_(this_->ssid_);
    }
}
