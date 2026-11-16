#ifndef _STACK_CHAN_WIFI_STATION_H_
#define _STACK_CHAN_WIFI_STATION_H_

#include <string>
#include <functional>
#include <mutex>
#include "wifi_failure.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>

/**
 * @brief A simplied WiFi station for config verifyation and config store.
 *
 */
class StackChanWifiStation {
public:
    StackChanWifiStation();
    ~StackChanWifiStation();

    void AddAuth(const std::string& ssid, const std::string& password, const WifiBandInfo& band = {});
    bool Start();
    void Stop();
    void Poll();
    bool IsConnected();
    bool WaitForConnected(int timeout_ms = 10000);
    int8_t GetRssi();
    std::string GetSsid() const;
    std::string GetIpAddress() const;
    uint8_t GetChannel();
    void SetPowerSaveMode(bool enabled);

    void OnConnect(std::function<void(const std::string& ssid)> on_connect);
    void OnConnected(std::function<void(const std::string& ssid)> on_connected);
    void OnConnectFailed(std::function<void(const std::string&, const WifiConnectionFailure&)> on_connect_failed);

private:
    EventGroupHandle_t event_group_;
    esp_event_handler_instance_t instance_any_id_ = nullptr;
    esp_event_handler_instance_t instance_got_ip_ = nullptr;
    esp_netif_t* station_netif_                   = nullptr;
    std::string ssid_;
    WifiBandInfo band_;
    std::string ip_address_;
    std::string password_;
    mutable std::mutex mutex_;
    int64_t connection_started_at_ = 0;
    bool associated_               = false;
    bool waiting_for_disconnect_   = false;
    bool owns_netif_               = false;
    bool owns_wifi_driver_         = false;
    int start_error_               = 0;
    int reconnect_count_           = 0;
    bool is_started_               = false;
    bool is_connecting_            = false;
    std::function<void(const std::string& ssid)> on_connect_;
    std::function<void(const std::string& ssid)> on_connected_;
    std::function<void(const std::string&, const WifiConnectionFailure&)> on_connect_failed_;

    void connectCurrent();
    void fail(WifiConnectionFailure failure);

    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

#endif  // _STACK_CHAN_WIFI_STATION_H_
