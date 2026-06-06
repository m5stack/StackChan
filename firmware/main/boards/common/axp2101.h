#pragma once

#include "i2c_device.h"

class Axp2101 : public I2cDevice {
public:
    Axp2101(i2c_master_bus_handle_t bus, uint8_t address);

    bool IsCharging();
    bool IsDischarging();
    bool IsChargingDone();
    int GetBatteryLevel();
    float GetTemperature();
    void PowerOff();

private:
    enum class BatteryCurrentDirection : uint8_t {
        Unknown = 0,
        Charging = 1,
        Discharging = 2,
    };

    BatteryCurrentDirection GetBatteryCurrentDirection();
};
