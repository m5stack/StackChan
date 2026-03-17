/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "PCF8563_Class.hpp"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char* TAG = "PCF8563";

namespace m5 {

static std::uint8_t bcd2ToByte(std::uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

static std::uint8_t byteToBcd2(std::uint8_t value)
{
    std::uint_fast8_t bcdhigh = value / 10;
    return (bcdhigh << 4) | (value - (bcdhigh * 10));
}

PCF8563_Class::PCF8563_Class(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr) : _addr(addr), _init(false)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = _addr,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &_i2c_dev));
}

PCF8563_Class::~PCF8563_Class()
{
    if (_i2c_dev) {
        i2c_master_bus_rm_device(_i2c_dev);
    }
}

bool PCF8563_Class::begin()
{
    // TimerCamera's internal RTC sometimes failed to initialize, so execute a dummy write first
    writeRegister8(0x00, 0x00);
    _init = (writeRegister8(0x00, 0x00) == ESP_OK) && (writeRegister8(0x0E, 0x03) == ESP_OK);
    return _init;
}

esp_err_t PCF8563_Class::writeRegister8(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(_i2c_dev, buf, sizeof(buf), 1000);
}

uint8_t PCF8563_Class::readRegister8(uint8_t reg)
{
    uint8_t val   = 0;
    esp_err_t err = i2c_master_transmit_receive(_i2c_dev, &reg, 1, &val, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "readRegister8 failed: %s", esp_err_to_name(err));
        return 0;
    }
    return val;
}

esp_err_t PCF8563_Class::writeRegister(uint8_t reg, const uint8_t* data, size_t len)
{
    if (len == 0) return ESP_OK;
    uint8_t* buf = (uint8_t*)malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    buf[0] = reg;
    memcpy(buf + 1, data, len);

    esp_err_t err = i2c_master_transmit(_i2c_dev, buf, len + 1, 1000);
    free(buf);
    return err;
}

esp_err_t PCF8563_Class::readRegister(uint8_t reg, uint8_t* data, size_t len)
{
    return i2c_master_transmit_receive(_i2c_dev, &reg, 1, data, len, 1000);
}

esp_err_t PCF8563_Class::bitOn(uint8_t reg, uint8_t mask)
{
    uint8_t val = readRegister8(reg);
    return writeRegister8(reg, val | mask);
}

esp_err_t PCF8563_Class::bitOff(uint8_t reg, uint8_t mask)
{
    uint8_t val = readRegister8(reg);
    return writeRegister8(reg, val & ~mask);
}

bool PCF8563_Class::getVoltLow(void)
{
    return readRegister8(0x02) & 0x80;  // RTCC_VLSEC_MASK
}

bool PCF8563_Class::getDateTime(rtc_date_t* date, rtc_time_t* time)
{
    if (!_init) return false;

    std::uint8_t buf[7] = {0};
    int start_reg       = (time != nullptr) ? 0x02 : 0x05;
    int len             = ((date != nullptr) ? 4 : 0) + ((time != nullptr) ? 3 : 0);

    if (len == 0) return false;

    if (readRegister(start_reg, buf, len) != ESP_OK) {
        return false;
    }

    int idx = 0;
    if (time) {
        time->seconds = bcd2ToByte(buf[idx++] & 0x7f);
        time->minutes = bcd2ToByte(buf[idx++] & 0x7f);
        time->hours   = bcd2ToByte(buf[idx++] & 0x3f);
    }

    if (date) {
        date->date    = bcd2ToByte(buf[idx++] & 0x3f);
        date->weekDay = bcd2ToByte(buf[idx++] & 0x07);
        date->month   = bcd2ToByte(buf[idx++] & 0x1f);
        date->year    = bcd2ToByte(buf[idx] & 0xff) + ((0x80 & buf[idx - 1]) ? 1900 : 2000);
    }
    return true;
}

bool PCF8563_Class::setDateTime(const rtc_date_t* date, const rtc_time_t* time)
{
    if (!_init) return false;

    std::uint8_t buf[7] = {0};

    int idx       = 0;
    int reg_start = 0x05;
    if (time) {
        reg_start  = 0x02;
        buf[idx++] = byteToBcd2(time->seconds);
        buf[idx++] = byteToBcd2(time->minutes);
        buf[idx++] = byteToBcd2(time->hours);
    }

    if (date) {
        buf[idx++] = byteToBcd2(date->date);
        buf[idx++] = (uint8_t)(0x07u & date->weekDay);
        buf[idx++] = (std::uint8_t)(byteToBcd2(date->month) + (date->year < 2000 ? 0x80 : 0));
        buf[idx++] = byteToBcd2(date->year % 100);
    }
    if (idx == 0) {
        return false;
    }
    return writeRegister(reg_start, buf, idx) == ESP_OK;
}

std::uint32_t PCF8563_Class::setTimerIRQ(std::uint32_t msec)
{
    if (!_init) return 0;

    std::uint8_t reg_value = readRegister8(0x01) & ~0x0C;

    std::uint32_t afterSeconds = (msec + 500) / 1000;
    if (afterSeconds <= 0) {  // disable timer
        writeRegister8(0x01, reg_value & ~0x01);
        writeRegister8(0x0E, 0x03);
        return 0;
    }

    std::size_t div         = 1;
    std::uint8_t type_value = 0x82;
    if (afterSeconds < 270) {
        if (afterSeconds > 255) {
            afterSeconds = 255;
        }
    } else {
        div          = 60;
        afterSeconds = (afterSeconds + 30) / div;
        if (afterSeconds > 255) {
            afterSeconds = 255;
        }
        type_value = 0x83;
    }

    writeRegister8(0x0E, type_value);
    writeRegister8(0x0F, afterSeconds);

    writeRegister8(0x01, (reg_value | 0x01) & ~0x80);
    return afterSeconds * div * 1000;
}

int PCF8563_Class::setAlarmIRQ(const rtc_date_t* date, const rtc_time_t* time)
{
    if (!_init) return 0;

    union {
        std::uint32_t raw;
        std::uint8_t buf[4];
    } data_u;

    data_u.raw = ~0;  // 0xFFFFFFFF

    bool irq_enable = false;
    if (time) {
        if (time->minutes >= 0) {
            irq_enable    = true;
            data_u.buf[0] = byteToBcd2(time->minutes) & 0x7f;
        }

        if (time->hours >= 0) {
            irq_enable    = true;
            data_u.buf[1] = byteToBcd2(time->hours) & 0x3f;
        }
    }
    if (date) {
        if (date->date >= 0) {
            irq_enable    = true;
            data_u.buf[2] = byteToBcd2(date->date) & 0x3f;
        }

        if (date->weekDay >= 0) {
            irq_enable    = true;
            data_u.buf[3] = byteToBcd2(date->weekDay) & 0x07;
        }
    }

    writeRegister(0x09, data_u.buf, 4);

    if (irq_enable) {
        bitOn(0x01, 0x02);
    } else {
        bitOff(0x01, 0x02);
    }

    return irq_enable;
}

bool PCF8563_Class::getIRQstatus(void)
{
    return _init && (0x0C & readRegister8(0x01));
}

void PCF8563_Class::clearIRQ(void)
{
    if (!_init) {
        return;
    }
    bitOff(0x01, 0x0C);
}

void PCF8563_Class::disableIRQ(void)
{
    if (!_init) {
        return;
    }
    // disable alerm (bit7:1=disabled)
    static constexpr const std::uint8_t buf[4] = {0x80, 0x80, 0x80, 0x80};
    writeRegister(0x09, buf, 4);

    // disable timer (bit7:0=disabled)
    writeRegister8(0x0E, 0);

    // clear flag and INT enable bits
    writeRegister8(0x01, 0x00);
}

}  // namespace m5
