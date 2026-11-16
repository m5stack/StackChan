#include "wifi_station.h"
#include <cstring>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "ssid_manager.h"
#include <wifi_manager.h>

#define TAG                  "StackChanWifiStation"
#define WIFI_EVENT_CONNECTED BIT0
#define MAX_RECONNECT_COUNT  1

StackChanWifiStation::StackChanWifiStation() : event_group_(xEventGroupCreate())
{
}

StackChanWifiStation::~StackChanWifiStation()
{
    Stop();
    if (event_group_) vEventGroupDelete(event_group_);
}

void StackChanWifiStation::AddAuth(const std::string& ssid, const std::string& password, const WifiBandInfo& band)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ssid_     = ssid;
        password_ = password;
        band_     = band;
        ip_address_.clear();
        reconnect_count_        = 0;
        associated_             = false;
        waiting_for_disconnect_ = false;
        is_connecting_          = true;
        connection_started_at_  = esp_timer_get_time();
    }
    if (event_group_) xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    if (ssid.empty() || ssid.size() > 32 || ssid.find('\0') != std::string::npos) {
        fail({"invalid_ssid", "Wi-Fi SSID must contain 1-32 bytes and no NUL characters."});
        return;
    }
    if (password.size() > 64 || password.find('\0') != std::string::npos) {
        fail({"invalid_password", "Wi-Fi password must contain at most 64 bytes and no NUL characters."});
        return;
    }
    if (!is_started_) {
        fail({"wifi_unavailable", "Wi-Fi could not be initialized: " + std::string(esp_err_to_name(start_error_)), 0,
              start_error_});
        return;
    }
    if (on_connect_) on_connect_(ssid);

    wifi_ap_record_t ap_info{};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            waiting_for_disconnect_ = true;
        }
        const auto error = esp_wifi_disconnect();
        if (error != ESP_OK)
            fail({"disconnect_failed",
                  "Could not disconnect the previous Wi-Fi network: " + std::string(esp_err_to_name(error)), 0, error});
    } else {
        connectCurrent();
    }
}

void StackChanWifiStation::connectCurrent()
{
    wifi_config_t config{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_connecting_) return;
        memcpy(config.sta.ssid, ssid_.data(), ssid_.size());
        memcpy(config.sta.password, password_.data(), password_.size());
    }
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    auto error             = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (error != ESP_OK) {
        fail({"invalid_configuration", "Wi-Fi configuration was rejected: " + std::string(esp_err_to_name(error)), 0,
              error});
        return;
    }
    error = esp_wifi_connect();
    if (error != ESP_OK)
        fail({"connect_failed", "Could not start Wi-Fi connection: " + std::string(esp_err_to_name(error)), 0, error});
}

void StackChanWifiStation::fail(WifiConnectionFailure failure)
{
    std::function<void(const std::string&, const WifiConnectionFailure&)> callback;
    std::string ssid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_connecting_) return;
        is_connecting_          = false;
        waiting_for_disconnect_ = false;
        password_.clear();
        ip_address_.clear();
        ssid     = ssid_;
        callback = on_connect_failed_;
    }
    if (event_group_) xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    esp_wifi_disconnect();
    if (callback) callback(ssid, failure);
}

void StackChanWifiStation::Poll()
{
    bool expired;
    bool associated;
    WifiBandInfo band;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        expired    = is_connecting_ && esp_timer_get_time() - connection_started_at_ >= 30000000;
        associated = associated_;
        band       = band_;
    }
    if (!expired) return;
    if (associated) {
        fail(WifiConnectionFailure{
            "dhcp_timeout",
            "Connected to the router, but no IP address was assigned. Check the router DHCP settings."});
    } else if (band.has_5ghz_hint()) {
        // The App explicitly reported 5 GHz, and the device never associated.
        fail(wifiNot2G4Failure());
    } else {
        fail(WifiConnectionFailure{"connection_timeout",
                                   "Wi-Fi connection timed out. Check the SSID, password and router range."});
    }
}

bool StackChanWifiStation::Start()
{
    if (is_started_) return true;
    if (!event_group_) {
        start_error_ = ESP_ERR_NO_MEM;
        return false;
    }
    start_error_ = esp_netif_init();
    if (start_error_ != ESP_OK && start_error_ != ESP_ERR_INVALID_STATE) return false;
    start_error_ = esp_event_loop_create_default();
    if (start_error_ != ESP_OK && start_error_ != ESP_ERR_INVALID_STATE) return false;
    station_netif_ = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!station_netif_) {
        station_netif_ = esp_netif_create_default_wifi_sta();
        owns_netif_    = true;
    }
    if (!station_netif_) {
        start_error_ = ESP_ERR_NO_MEM;
        return false;
    }
    if (!WifiManager::GetInstance().IsInitialized()) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        config.nvs_enable         = true;
        start_error_              = esp_wifi_init(&config);
        if (start_error_ != ESP_OK) {
            Stop();
            return false;
        }
        owns_wifi_driver_ = true;
    }
    start_error_ =
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, this, &instance_any_id_);
    if (start_error_ != ESP_OK) {
        Stop();
        return false;
    }
    start_error_ =
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &IpEventHandler, this, &instance_got_ip_);
    if (start_error_ != ESP_OK) {
        Stop();
        return false;
    }
    if ((start_error_ = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (start_error_ = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK || (start_error_ = esp_wifi_start()) != ESP_OK) {
        Stop();
        return false;
    }
    is_started_ = true;
    return true;
}

void StackChanWifiStation::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_connecting_ = false;
        password_.clear();
    }
    if (instance_any_id_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
        instance_got_ip_ = nullptr;
    }
    if (is_started_ || owns_netif_) esp_wifi_stop();
    // WifiManager keeps its driver initialized when stopping station mode.
    // Destroying our netif must not invalidate that manager's driver state.
    if (owns_wifi_driver_) esp_wifi_deinit();
    if (owns_netif_) {
        esp_netif_destroy_default_wifi(station_netif_);
    }
    station_netif_    = nullptr;
    owns_netif_       = false;
    owns_wifi_driver_ = false;
    is_started_       = false;
    if (event_group_) xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
}

void StackChanWifiStation::OnConnect(std::function<void(const std::string&)> callback)
{
    on_connect_ = std::move(callback);
}
void StackChanWifiStation::OnConnected(std::function<void(const std::string&)> callback)
{
    on_connected_ = std::move(callback);
}
void StackChanWifiStation::OnConnectFailed(
    std::function<void(const std::string&, const WifiConnectionFailure&)> callback)
{
    on_connect_failed_ = std::move(callback);
}
std::string StackChanWifiStation::GetSsid() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ssid_;
}
std::string StackChanWifiStation::GetIpAddress() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ip_address_;
}
bool StackChanWifiStation::IsConnected()
{
    return event_group_ && (xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED);
}

bool StackChanWifiStation::WaitForConnected(int timeout_ms)
{
    return event_group_ &&
           (xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms)) &
            WIFI_EVENT_CONNECTED);
}
int8_t StackChanWifiStation::GetRssi()
{
    wifi_ap_record_t ap{};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}
uint8_t StackChanWifiStation::GetChannel()
{
    wifi_ap_record_t ap{};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.primary : 0;
}
void StackChanWifiStation::SetPowerSaveMode(bool enabled)
{
    esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

void StackChanWifiStation::WifiEventHandler(void* arg, esp_event_base_t, int32_t event_id, void* event_data)
{
    auto* station = static_cast<StackChanWifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        std::lock_guard<std::mutex> lock(station->mutex_);
        if (station->is_connecting_ && !station->waiting_for_disconnect_) station->associated_ = true;
        return;
    }
    if (event_id != WIFI_EVENT_STA_DISCONNECTED) return;
    xEventGroupClearBits(station->event_group_, WIFI_EVENT_CONNECTED);
    bool configure = false;
    bool retry     = false;
    WifiBandInfo band;
    {
        std::lock_guard<std::mutex> lock(station->mutex_);
        station->ip_address_.clear();
        station->associated_ = false;
        if (!station->is_connecting_) return;
        band = station->band_;
        if (station->waiting_for_disconnect_) {
            station->waiting_for_disconnect_ = false;
            configure                        = true;
        } else if (station->reconnect_count_ < MAX_RECONNECT_COUNT) {
            ++station->reconnect_count_;
            retry = true;
        }
    }
    if (configure) {
        station->connectCurrent();
        return;
    }
    if (retry) {
        const auto error = esp_wifi_connect();
        if (error != ESP_OK)
            station->fail({"connect_failed", "Could not retry Wi-Fi connection: " + std::string(esp_err_to_name(error)),
                           0, error});
        return;
    }
    const auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
    station->fail(wifiDisconnectFailure(event ? event->reason : 0, band));
}

void StackChanWifiStation::IpEventHandler(void* arg, esp_event_base_t, int32_t, void* event_data)
{
    auto* station     = static_cast<StackChanWifiStation*>(arg);
    const auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    if (!event) return;
    std::string ssid, password;
    {
        std::lock_guard<std::mutex> lock(station->mutex_);
        // Ignore IP events from the old AP or a timed-out/cancelled attempt.
        if (!station->is_connecting_ || station->waiting_for_disconnect_ || !station->associated_) return;
        char ip[16]{};
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        station->ip_address_      = ip;
        ssid                      = station->ssid_;
        password                  = std::move(station->password_);
        station->reconnect_count_ = 0;
        station->is_connecting_   = false;
    }
    // Replace stored credentials only after the new network has assigned an IP.
    SsidManager::GetInstance().AddSsid(ssid, password);
    xEventGroupSetBits(station->event_group_, WIFI_EVENT_CONNECTED);
    if (station->on_connected_) station->on_connected_(ssid);
}
