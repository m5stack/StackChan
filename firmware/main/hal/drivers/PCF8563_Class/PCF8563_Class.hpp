/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace m5 {

struct rtc_time_t {
    int8_t hours;
    int8_t minutes;
    int8_t seconds;
};

struct rtc_date_t {
    int8_t weekDay;
    int8_t month;
    int8_t date;
    int16_t year;
};

class PCF8563_Class {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x51;

    PCF8563_Class(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr = DEFAULT_ADDRESS);
    ~PCF8563_Class();

    bool begin();

    bool getDateTime(rtc_date_t* date, rtc_time_t* time);
    bool setDateTime(const rtc_date_t* date, const rtc_time_t* time);

    /// Set timer IRQ
    uint32_t setTimerIRQ(uint32_t timer_msec);

    /// Set alarm by time
    int setAlarmIRQ(const rtc_date_t* date, const rtc_time_t* time);

    bool getIRQstatus(void);
    void clearIRQ(void);
    void disableIRQ(void);

    bool getVoltLow(void);

private:
    i2c_master_dev_handle_t _i2c_dev;
    uint8_t _addr;
    bool _init;

    esp_err_t writeRegister8(uint8_t reg, uint8_t value);
    uint8_t readRegister8(uint8_t reg);
    esp_err_t writeRegister(uint8_t reg, const uint8_t* data, size_t len);
    esp_err_t readRegister(uint8_t reg, uint8_t* data, size_t len);

    esp_err_t bitOn(uint8_t reg, uint8_t mask);
    esp_err_t bitOff(uint8_t reg, uint8_t mask);
};
}  // namespace m5
