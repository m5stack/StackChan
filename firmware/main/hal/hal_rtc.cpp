/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "board/hal_bridge.h"
#include "drivers/PCF8563_Class/PCF8563_Class.hpp"
#include <mooncake_log.h>
#include <memory>
#include <sys/time.h>
#include <ctime>
#include <settings.h>

static const std::string_view _tag = "HAL-RTC";

static std::unique_ptr<m5::PCF8563_Class> _pcf8563;

void Hal::rtc_init()
{
    mclog::tagInfo(_tag, "init");

    auto i2c_bus = hal_bridge::board_get_i2c_bus();

    _pcf8563 = std::make_unique<m5::PCF8563_Class>(i2c_bus);
    if (!_pcf8563->begin()) {
        _pcf8563.reset();
        mclog::tagError(_tag, "PCF8563 init failed");
        return;
    }
    mclog::tagInfo(_tag, "PCF8563 init ok");

    // Load timezone from settings
    std::string tz = getTimezone();
    setenv("TZ", tz.c_str(), 1);
    tzset();
    mclog::tagInfo(_tag, "load timezone from nvs: {}", tz);

    syncRtcTimeToSystem();
}

void Hal::syncRtcTimeToSystem()
{
    if (!_pcf8563) {
        return;
    }

    m5::rtc_date_t date;
    m5::rtc_time_t time;
    if (_pcf8563->getDateTime(&date, &time)) {
        struct tm tm_curr = {0};
        tm_curr.tm_year   = date.year - 1900;
        tm_curr.tm_mon    = date.month - 1;
        tm_curr.tm_mday   = date.date;
        tm_curr.tm_hour   = time.hours;
        tm_curr.tm_min    = time.minutes;
        tm_curr.tm_sec    = time.seconds;
        tm_curr.tm_isdst  = 0;

        // Temporarily set TZ to UTC to interpret RTC time as UTC
        std::string current_tz = getenv("TZ") ? getenv("TZ") : "";
        setenv("TZ", "UTC0", 1);
        tzset();

        time_t t = mktime(&tm_curr);

        // Restore original TZ
        if (!current_tz.empty()) {
            setenv("TZ", current_tz.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();

        struct timeval tv = {.tv_sec = t, .tv_usec = 0};
        settimeofday(&tv, NULL);
        mclog::tagInfo(_tag, "rtc synced to system (UTC): {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", date.year,
                       date.month, date.date, time.hours, time.minutes, time.seconds);
    } else {
        mclog::tagError(_tag, "failed to read rtc");
    }
}

void Hal::syncSystemTimeToRtc()
{
    if (!_pcf8563) {
        return;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_curr;
    gmtime_r(&tv.tv_sec, &tm_curr);

    m5::rtc_date_t date;
    m5::rtc_time_t time;
    date.year    = tm_curr.tm_year + 1900;
    date.month   = tm_curr.tm_mon + 1;
    date.date    = tm_curr.tm_mday;
    date.weekDay = tm_curr.tm_wday;
    time.hours   = tm_curr.tm_hour;
    time.minutes = tm_curr.tm_min;
    time.seconds = tm_curr.tm_sec;

    if (_pcf8563->setDateTime(&date, &time)) {
        mclog::tagInfo(_tag, "system synced to rtc (UTC): {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", date.year,
                       date.month, date.date, time.hours, time.minutes, time.seconds);
    } else {
        mclog::tagError(_tag, "failed to write rtc");
    }
}

void Hal::setTimezone(std::string_view tz)
{
    setenv("TZ", tz.data(), 1);
    tzset();
    Settings settings("system", true);
    settings.SetString("tz", std::string(tz));
    mclog::tagInfo(_tag, "timezone updated to: {}", tz);
}

std::string Hal::getTimezone()
{
    Settings settings("system", false);
    return settings.GetString("tz", "GMT0");
}
