/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "PY32IOExpander_Class.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "PY32IOExpander";

namespace m5 {

// Register definitions
static constexpr uint8_t REG_UID_L      = 0x00;
static constexpr uint8_t REG_UID_H      = 0x01;
static constexpr uint8_t REG_VERSION    = 0x02;
static constexpr uint8_t REG_GPIO_M_L   = 0x03;
static constexpr uint8_t REG_GPIO_M_H   = 0x04;
static constexpr uint8_t REG_GPIO_O_L   = 0x05;
static constexpr uint8_t REG_GPIO_O_H   = 0x06;
static constexpr uint8_t REG_GPIO_I_L   = 0x07;
static constexpr uint8_t REG_GPIO_I_H   = 0x08;
static constexpr uint8_t REG_GPIO_PU_L  = 0x09;
static constexpr uint8_t REG_GPIO_PU_H  = 0x0A;
static constexpr uint8_t REG_GPIO_PD_L  = 0x0B;
static constexpr uint8_t REG_GPIO_PD_H  = 0x0C;
static constexpr uint8_t REG_GPIO_IE_L  = 0x0D;
static constexpr uint8_t REG_GPIO_IE_H  = 0x0E;
static constexpr uint8_t REG_GPIO_IT_L  = 0x0F;
static constexpr uint8_t REG_GPIO_IT_H  = 0x10;
static constexpr uint8_t REG_GPIO_IS_L  = 0x11;
static constexpr uint8_t REG_GPIO_IS_H  = 0x12;
static constexpr uint8_t REG_GPIO_DRV_L = 0x13;
static constexpr uint8_t REG_GPIO_DRV_H = 0x14;

static constexpr uint8_t REG_ADC_CTRL = 0x15;
static constexpr uint8_t REG_ADC_D_L  = 0x16;
static constexpr uint8_t REG_ADC_D_H  = 0x17;

static constexpr uint8_t REG_PWM_FREQ_L = 0x25;
static constexpr uint8_t REG_PWM_FREQ_H = 0x26;

static constexpr uint8_t REG_LED_CFG       = 0x24;
static constexpr uint8_t REG_LED_RAM_START = 0x30;

// PWM Duty Registers
static constexpr uint8_t REG_PWM1_DUTY_L = 0x1B;
static constexpr uint8_t REG_PWM1_DUTY_H = 0x1C;
static constexpr uint8_t REG_PWM2_DUTY_L = 0x1D;
static constexpr uint8_t REG_PWM2_DUTY_H = 0x1E;
static constexpr uint8_t REG_PWM3_DUTY_L = 0x1F;
static constexpr uint8_t REG_PWM3_DUTY_H = 0x20;
static constexpr uint8_t REG_PWM4_DUTY_L = 0x21;
static constexpr uint8_t REG_PWM4_DUTY_H = 0x22;

PY32IOExpander_Class::PY32IOExpander_Class(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr)
    : _addr(addr), _initialized(false)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = _addr,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &_i2c_dev));
}

PY32IOExpander_Class::~PY32IOExpander_Class()
{
    if (_i2c_dev) {
        i2c_master_bus_rm_device(_i2c_dev);
    }
}

esp_err_t PY32IOExpander_Class::writeRegister8(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(_i2c_dev, buf, sizeof(buf), 1000);
}

uint8_t PY32IOExpander_Class::readRegister8(uint8_t reg)
{
    uint8_t val   = 0;
    esp_err_t err = i2c_master_transmit_receive(_i2c_dev, &reg, 1, &val, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "readRegister8 failed: %s", esp_err_to_name(err));
        return 0;
    }
    return val;
}

esp_err_t PY32IOExpander_Class::writeRegister(uint8_t reg, const uint8_t* data, size_t len)
{
    if (len == 0) return ESP_OK;

    // Allocate buffer for reg + data
    uint8_t* buf = (uint8_t*)malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    buf[0] = reg;
    memcpy(buf + 1, data, len);

    esp_err_t err = i2c_master_transmit(_i2c_dev, buf, len + 1, 1000);
    free(buf);
    return err;
}

esp_err_t PY32IOExpander_Class::readRegister(uint8_t reg, uint8_t* data, size_t len)
{
    return i2c_master_transmit_receive(_i2c_dev, &reg, 1, data, len, 1000);
}

esp_err_t PY32IOExpander_Class::bitOn(uint8_t reg, uint8_t mask)
{
    uint8_t val = readRegister8(reg);
    val |= mask;
    return writeRegister8(reg, val);
}

esp_err_t PY32IOExpander_Class::bitOff(uint8_t reg, uint8_t mask)
{
    uint8_t val = readRegister8(reg);
    val &= ~mask;
    return writeRegister8(reg, val);
}

void PY32IOExpander_Class::_writeBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin, bool value)
{
    if (pin < 8) {
        if (value)
            bitOn(reg_l, 1 << pin);
        else
            bitOff(reg_l, 1 << pin);
    } else {
        if (value)
            bitOn(reg_h, 1 << (pin - 8));
        else
            bitOff(reg_h, 1 << (pin - 8));
    }
}

bool PY32IOExpander_Class::_readBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin)
{
    if (pin < 8) {
        return (readRegister8(reg_l) & (1 << pin)) != 0;
    } else {
        return (readRegister8(reg_h) & (1 << (pin - 8))) != 0;
    }
}

bool PY32IOExpander_Class::begin()
{
    uint8_t version = readRegister8(REG_VERSION);
    if (version == 0 || version == 0xFF) {
        ESP_LOGE(TAG, "Invalid version: 0x%02X", version);
        return false;
    }
    ESP_LOGI(TAG, "Version: 0x%02X", version);
    _initialized = true;
    return true;
}

void PY32IOExpander_Class::setDirection(uint8_t pin, bool direction)
{
    // direction: false=input (0), true=output (1)
    _writeBit(REG_GPIO_M_L, REG_GPIO_M_H, pin, direction);
}

void PY32IOExpander_Class::enablePull(uint8_t pin, bool enablePull)
{
    if (enablePull) {
        // Enable Pull Up by default if neither is set
        bool pu = _readBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin);
        bool pd = _readBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin);
        if (!pu && !pd) {
            _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, true);
        }
        // If one is already set, leave it.
    } else {
        // Disable both
        _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, false);
        _writeBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin, false);
    }
}

void PY32IOExpander_Class::setPullMode(uint8_t pin, bool mode)
{
    // mode: false=down, true=up
    if (mode) {
        // Pull Up
        _writeBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin, false);
        _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, true);
    } else {
        // Pull Down
        _writeBit(REG_GPIO_PU_L, REG_GPIO_PU_H, pin, false);
        _writeBit(REG_GPIO_PD_L, REG_GPIO_PD_H, pin, true);
    }
}

void PY32IOExpander_Class::setDriveMode(uint8_t pin, bool openDrain)
{
    // openDrain: false=push-pull (0), true=open-drain (1)
    _writeBit(REG_GPIO_DRV_L, REG_GPIO_DRV_H, pin, openDrain);
}

void PY32IOExpander_Class::setHighImpedance(uint8_t pin, bool enable)
{
    if (enable) {
        // Input mode
        setDirection(pin, false);
        // Disable pulls
        enablePull(pin, false);
    }
}

bool PY32IOExpander_Class::getWriteValue(uint8_t pin)
{
    return _readBit(REG_GPIO_O_L, REG_GPIO_O_H, pin);
}

void PY32IOExpander_Class::digitalWrite(uint8_t pin, bool level)
{
    _writeBit(REG_GPIO_O_L, REG_GPIO_O_H, pin, level);
}

bool PY32IOExpander_Class::digitalRead(uint8_t pin)
{
    return _readBit(REG_GPIO_I_L, REG_GPIO_I_H, pin);
}

void PY32IOExpander_Class::resetIrq()
{
    // Clear all interrupts by writing 1s to IS registers
    writeRegister8(REG_GPIO_IS_L, 0xFF);
    writeRegister8(REG_GPIO_IS_H, 0xFF);  // Only bits 0-5 used for high byte (pins 8-13)
}

void PY32IOExpander_Class::disableIrq()
{
    // Disable all interrupts
    writeRegister8(REG_GPIO_IE_L, 0x00);
    writeRegister8(REG_GPIO_IE_H, 0x00);
}

void PY32IOExpander_Class::enableIrq()
{
    // Enable all interrupts
    writeRegister8(REG_GPIO_IE_L, 0xFF);
    writeRegister8(REG_GPIO_IE_H, 0x3F);  // Pins 8-13
}

uint16_t PY32IOExpander_Class::readDeviceUID()
{
    uint8_t l = readRegister8(REG_UID_L);
    uint8_t h = readRegister8(REG_UID_H);
    return (h << 8) | l;
}

uint8_t PY32IOExpander_Class::readVersion()
{
    return readRegister8(REG_VERSION);
}

uint16_t PY32IOExpander_Class::analogRead(uint8_t channel)
{
    if (channel < 1 || channel > 4) return 0;

    // Start conversion
    // REG_ADC_CTRL: [7:Busy] [6:Start] [2:0:Channel]
    // Channel mapping: 1->1, 2->2, 3->3, 4->4
    writeRegister8(REG_ADC_CTRL, (1 << 6) | (channel & 0x07));

    // Wait for busy bit to clear
    // Simple polling with timeout
    for (int i = 0; i < 100; i++) {
        uint8_t ctrl = readRegister8(REG_ADC_CTRL);
        if (!(ctrl & (1 << 7))) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint8_t l = readRegister8(REG_ADC_D_L);
    uint8_t h = readRegister8(REG_ADC_D_H);
    return (h << 8) | l;
}

void PY32IOExpander_Class::setPwmDuty(uint8_t channel, uint8_t duty)
{
    if (channel > 3) return;

    // Calculate register address
    // Channel 0 -> PWM1 (0x1B)
    // Channel 1 -> PWM2 (0x1D)
    // Channel 2 -> PWM3 (0x1F)
    // Channel 3 -> PWM4 (0x21)
    uint8_t reg_l = REG_PWM1_DUTY_L + (channel * 2);
    uint8_t reg_h = reg_l + 1;

    // Duty is 8-bit percentage (0-100)? Or 0-255?
    // m5_io_py32ioexpander uses percentage (0-100) or 12-bit raw.
    // Let's assume 0-255 for standard Arduino style, but map to 12-bit (0-4095).
    // 255 -> 4095. val * 4095 / 255 = val * 16 approx.
    uint16_t duty12 = (uint16_t)duty * 16;
    if (duty12 > 4095) duty12 = 4095;

    // High byte contains Enable(7) and Polarity(6) bits.
    // We need to preserve them or set defaults.
    // Let's enable by default, polarity normal (0).
    uint8_t h_val = (duty12 >> 8) & 0x0F;
    h_val |= (1 << 7);  // Enable

    writeRegister8(reg_l, duty12 & 0xFF);
    writeRegister8(reg_h, h_val);
}

void PY32IOExpander_Class::setPwmFrequency(uint16_t freq)
{
    writeRegister8(REG_PWM_FREQ_L, freq & 0xFF);
    writeRegister8(REG_PWM_FREQ_H, (freq >> 8) & 0xFF);
}

void PY32IOExpander_Class::setLedCount(uint8_t count)
{
    if (count > 32) count = 32;
    writeRegister8(REG_LED_CFG, count & 0x3F);
}

void PY32IOExpander_Class::setLedColor(uint8_t index, uint16_t color565)
{
    if (index >= 32) return;
    uint8_t data[2] = {(uint8_t)(color565 & 0xFF), (uint8_t)((color565 >> 8) & 0xFF)};
    writeRegister(REG_LED_RAM_START + index * 2, data, 2);
}

void PY32IOExpander_Class::setLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    // RGB888 to RGB565: RRRRRGGG GGGBBBBB
    uint16_t val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    setLedColor(index, val);
}

void PY32IOExpander_Class::setLedColor(uint8_t index, uint32_t color)
{
    setLedColor(index, (uint8_t)((color >> 16) & 0xFF), (uint8_t)((color >> 8) & 0xFF), (uint8_t)(color & 0xFF));
}

void PY32IOExpander_Class::setLedData(const uint8_t* data, size_t len)
{
    if (!data || len == 0) return;
    if (len > 64) len = 64;  // Max 32 LEDs * 2 bytes
    writeRegister(REG_LED_RAM_START, data, len);
}

void PY32IOExpander_Class::refreshLeds()
{
    uint8_t val = readRegister8(REG_LED_CFG);
    writeRegister8(REG_LED_CFG, val | (1 << 6));
}

}  // namespace m5
