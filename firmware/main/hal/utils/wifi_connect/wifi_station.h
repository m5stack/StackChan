#ifndef _WIFI_STATION_H_
#define _WIFI_STATION_H_

#include <string>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>

class StackChanWifiStation {
public:
    static StackChanWifiStation& GetInstance();
    void AddAuth(const std::string& ssid, const std::string& password);
    void Start();
    void Stop();
    bool IsConnected();
    bool WaitForConnected(int timeout_ms = 10000);
    int8_t GetRssi();
    std::string GetSsid() const
    {
        return ssid_;
    }
    std::string GetIpAddress() const
    {
        return ip_address_;
    }
    uint8_t GetChannel();
    void SetPowerSaveMode(bool enabled);

    void OnConnect(std::function<void(const std::string& ssid)> on_connect);
    void OnConnected(std::function<void(const std::string& ssid)> on_connected);
    void OnConnectFailed(std::function<void(const std::string& ssid)> on_connect_failed);

private:
    StackChanWifiStation();
    ~StackChanWifiStation();
    StackChanWifiStation(const StackChanWifiStation&) = delete;
    StackChanWifiStation& operator=(const StackChanWifiStation&) = delete;

    EventGroupHandle_t event_group_;
    esp_event_handler_instance_t instance_any_id_ = nullptr;
    esp_event_handler_instance_t instance_got_ip_ = nullptr;
    esp_netif_t* station_netif_                   = nullptr;
    std::string ssid_;
    std::string ip_address_;
    int reconnect_count_ = 0;
    bool is_started_     = false;
    bool is_connecting_  = false;
    std::function<void(const std::string& ssid)> on_connect_;
    std::function<void(const std::string& ssid)> on_connected_;
    std::function<void(const std::string& ssid)> on_connect_failed_;

    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

#endif  // _WIFI_STATION_H_
