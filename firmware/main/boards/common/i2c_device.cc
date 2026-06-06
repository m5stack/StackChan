#include "i2c_device.h"

namespace {

constexpr int kDefaultTimeoutMs = 100;
constexpr uint32_t kBusSpeedHz = 400 * 1000;

}  // namespace

I2cDevice::I2cDevice(i2c_master_bus_handle_t bus, uint8_t address)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = kBusSpeedHz,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &config, &device_));
}

I2cDevice::~I2cDevice()
{
    if (device_ != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_rm_device(device_));
    }
}

void I2cDevice::WriteReg(uint8_t reg, uint8_t value)
{
    const uint8_t tx[] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(device_, tx, sizeof(tx), kDefaultTimeoutMs));
}

uint8_t I2cDevice::ReadReg(uint8_t reg)
{
    uint8_t value = 0;
    ReadRegs(reg, &value, sizeof(value));
    return value;
}

void I2cDevice::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length)
{
    ESP_ERROR_CHECK(TryReadRegs(reg, buffer, length, kDefaultTimeoutMs));
}

esp_err_t I2cDevice::TryReadRegs(uint8_t reg, uint8_t* buffer, size_t length, int timeout_ms)
{
    if (buffer == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(device_, &reg, sizeof(reg), buffer, length, timeout_ms);
}
