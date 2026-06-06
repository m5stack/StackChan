#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <driver/i2c_master.h>

class I2cDevice {
public:
    I2cDevice(i2c_master_bus_handle_t bus, uint8_t address);
    virtual ~I2cDevice();

    I2cDevice(const I2cDevice&) = delete;
    I2cDevice& operator=(const I2cDevice&) = delete;

protected:
    void WriteReg(uint8_t reg, uint8_t value);
    uint8_t ReadReg(uint8_t reg);
    void ReadRegs(uint8_t reg, uint8_t* buffer, size_t length);
    esp_err_t TryReadRegs(uint8_t reg, uint8_t* buffer, size_t length, int timeout_ms = 100);

private:
    i2c_master_dev_handle_t device_ = nullptr;
};
