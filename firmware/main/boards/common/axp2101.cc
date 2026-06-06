#include "axp2101.h"

namespace {

constexpr uint8_t kPowerStatusReg = 0x01;
constexpr uint8_t kPowerControlReg = 0x10;
constexpr uint8_t kBatteryPercentReg = 0xA4;
constexpr uint8_t kTemperatureReg = 0xA5;

constexpr uint8_t kCurrentDirectionMask = 0b01100000;
constexpr uint8_t kCurrentDirectionShift = 5;
constexpr uint8_t kChargeStateMask = 0b00000111;
constexpr uint8_t kChargeDoneState = 0b00000100;
constexpr uint8_t kPowerOffBit = 0x01;

}  // namespace

Axp2101::Axp2101(i2c_master_bus_handle_t bus, uint8_t address)
    : I2cDevice(bus, address)
{
}

Axp2101::BatteryCurrentDirection Axp2101::GetBatteryCurrentDirection()
{
    const uint8_t status = ReadReg(kPowerStatusReg);
    return static_cast<BatteryCurrentDirection>((status & kCurrentDirectionMask) >> kCurrentDirectionShift);
}

bool Axp2101::IsCharging()
{
    return GetBatteryCurrentDirection() == BatteryCurrentDirection::Charging;
}

bool Axp2101::IsDischarging()
{
    return GetBatteryCurrentDirection() == BatteryCurrentDirection::Discharging;
}

bool Axp2101::IsChargingDone()
{
    const uint8_t status = ReadReg(kPowerStatusReg);
    return (status & kChargeStateMask) == kChargeDoneState;
}

int Axp2101::GetBatteryLevel()
{
    return ReadReg(kBatteryPercentReg);
}

float Axp2101::GetTemperature()
{
    return ReadReg(kTemperatureReg);
}

void Axp2101::PowerOff()
{
    WriteReg(kPowerControlReg, ReadReg(kPowerControlReg) | kPowerOffBit);
}
