#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>

#include "board.h"

class WifiBoard : public Board {
protected:
    esp_timer_handle_t connect_timer_ = nullptr;
    bool in_config_mode_ = false;
    NetworkEventCallback network_event_callback_ = nullptr;

    std::string GetBoardJson() override;
    void OnNetworkEvent(NetworkEvent event, const std::string& data = "");
    void TryWifiConnect();
    void StartWifiConfigMode();
    static void OnWifiConnectTimeout(void* arg);

public:
    WifiBoard();
    ~WifiBoard() override;

    std::string GetBoardType() override;
    void StartNetwork() override;
    NetworkInterface* GetNetwork() override;
    void SetNetworkEventCallback(NetworkEventCallback callback) override;
    const char* GetNetworkStateIcon() override;
    void SetPowerSaveLevel(PowerSaveLevel level) override;
    AudioCodec* GetAudioCodec() override { return nullptr; }
    std::string GetDeviceStatusJson() override;

    void EnterWifiConfigMode();
    bool IsInWifiConfigMode() const;
};
