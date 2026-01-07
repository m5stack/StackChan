/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <memory>
#include <mooncake_log.h>
#include <nvs_flash.h>

static std::unique_ptr<Hal> _hal_instance;
static const std::string _tag = "HAL";

Hal& GetHAL()
{
    if (!_hal_instance) {
        mclog::tagInfo(_tag, "creating hal instance");
        _hal_instance = std::make_unique<Hal>();
    }
    return *_hal_instance.get();
}

void Hal::init()
{
    mclog::tagInfo(_tag, "init");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ble_init();
    xiaozhi_board_init();
    xiaozhi_mcp_init();
    head_touch_init();
    io_expander_init();
    imu_init();
    servo_init();
    lvgl_init();
    reminder_init();

    // startWebSocketAvatar();
}

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_mac.h>

void Hal::delay(std::uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

std::uint32_t Hal::millis()
{
    return esp_timer_get_time() / 1000;
}

void Hal::feedTheDog()
{
    vTaskDelay(1);
}

std::array<uint8_t, 6> Hal::getFactoryMac()
{
    std::array<uint8_t, 6> mac;
    esp_efuse_mac_get_default(mac.data());
    return mac;
}

std::string Hal::getFactoryMacString(std::string divider)
{
    auto mac = getFactoryMac();
    return fmt::format("{:02X}{}{:02X}{}{:02X}{}{:02X}{}{:02X}{}{:02X}", mac[0], divider, mac[1], divider, mac[2],
                       divider, mac[3], divider, mac[4], divider, mac[5]);
}

void Hal::reboot()
{
    esp_restart();
}

/* -------------------------------------------------------------------------- */
/*                                   Xiaozhi                                  */
/* -------------------------------------------------------------------------- */
#include "board/hal_bridge.h"

void Hal::xiaozhi_board_init()
{
    mclog::tagInfo(_tag, "xiaozhi board init");

    hal_bridge::xiaozhi_board_init();
}

void Hal::startXiaozhi()
{
    mclog::tagInfo(_tag, "start xiaozhi");

    hal_bridge::start_xiaozhi_app();
}

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */
#include "board/hal_bridge.h"

void Hal::lvglLock()
{
    hal_bridge::disply_lvgl_lock();
}

void Hal::lvglUnlock()
{
    hal_bridge::disply_lvgl_unlock();
}

/* -------------------------------------------------------------------------- */
/*                                    Lvgl                                    */
/* -------------------------------------------------------------------------- */
#include "board/hal_bridge.h"
#include <stackchan/stackchan.h>

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    hal_bridge::lock();
    auto bridge_data = hal_bridge::get_data();

    if (bridge_data.isXiaozhiMode) {
        hal_bridge::unlock();
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // mclog::tagInfo(_tag, "touchpoint: {}, x: {}, y: {}", bridge_data.touchPoint.num, bridge_data.touchPoint.x,
    //                bridge_data.touchPoint.y);

    if (bridge_data.touchPoint.num == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
    } else {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = bridge_data.touchPoint.x;
        data->point.y = bridge_data.touchPoint.y;
    }

    hal_bridge::unlock();
}

void Hal::lvgl_init()
{
    mclog::tagInfo(_tag, "lvgl init");

    hal_bridge::disply_lvgl_lock();

    mclog::tagInfo(_tag, "create lvgl touchpad indev");
    lvTouchpad = lv_indev_create();
    lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvTouchpad, lvgl_read_cb);
    lv_indev_set_group(lvTouchpad, lv_group_get_default());
    lv_indev_set_display(lvTouchpad, hal_bridge::display_get_lvgl_display());

    hal_bridge::disply_lvgl_unlock();
}

lv_timer_t* _timer_stackchan_update = NULL;

void Hal::startStackChanAutoUpdate(int fps)
{
    mclog::tagInfo(_tag, "start stack chan auto update with fps: {}", fps);

    if (_timer_stackchan_update) {
        mclog::tagWarn(_tag, "stack chan auto update already started");
        return;
    }

    _timer_stackchan_update = lv_timer_create([](lv_timer_t* timer) { GetStackChan().update(); }, 1000 / fps, NULL);
}

void Hal::stopStackChanAutoUpdate()
{
    mclog::tagInfo(_tag, "stop stack chan auto update");

    if (!_timer_stackchan_update) {
        mclog::tagWarn(_tag, "stack chan auto update already stopped");
        return;
    }

    lv_timer_delete(_timer_stackchan_update);
    _timer_stackchan_update = NULL;
}

/* -------------------------------------------------------------------------- */
/*                                  Reminder                                  */
/* -------------------------------------------------------------------------- */
#include "hal/utils/reminder/reminder.h"

int Hal::createReminder(int duration_s, const std::string& message)
{
    return ReminderManager::GetInstance().CreateReminder(duration_s, message);
}

void Hal::stopReminder(int id)
{
    ReminderManager::GetInstance().StopReminder(id);
}

void Hal::reminder_init()
{
    mclog::tagInfo(_tag, "reminder init");

    ReminderManager::GetInstance().Start();
}
