#pragma once

#include <functional>
#include <string>

#include <network_interface.h>

#include "assets.h"
#include "boards/common/backlight.h"
#include "camera.h"
#include "led/led.h"

enum class NetworkEvent {
    Scanning,
    Connecting,
    Connected,
    Disconnected,
    WifiConfigModeEnter,
    WifiConfigModeExit,
    ModemDetecting,
    ModemErrorNoSim,
    ModemErrorRegDenied,
    ModemErrorInitFailed,
    ModemErrorTimeout,
};

enum class PowerSaveLevel {
    LOW_POWER,
    BALANCED,
    PERFORMANCE,
};

using NetworkEventCallback = std::function<void(NetworkEvent event, const std::string& data)>;

void* create_board();

class AudioCodec;
class Display;

class Board {
private:
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;

protected:
    Board();
    std::string GenerateUuid();

    std::string uuid_;

public:
    static Board& GetInstance()
    {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;

    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool GetTemperature(float& esp32_temperature);
    virtual Display* GetDisplay();
    virtual Camera* GetCamera();
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void StartNetwork() = 0;
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) { (void)callback; }
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging);
    virtual std::string GetSystemInfoJson();
    virtual void SetPowerSaveLevel(PowerSaveLevel level) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}
