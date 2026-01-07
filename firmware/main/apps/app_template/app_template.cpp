/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_template.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppTemplate::AppTemplate()
{
    // Configure App name
    setAppInfo().name = "AppTemplate";
    // Configure App icon
    // setAppInfo().icon = (void*)&icon_app_dummy;
}

// Called when the App is installed
void AppTemplate::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

static std::unique_ptr<Button> _button_quit;
static uint32_t _time_count = 0;

// Called when the App is opened
// You can construct UI, initialize operations, etc. here
void AppTemplate::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Lock before Lvgl operations to avoid conflict with lvgl thread
    // GetHAL().lvglLock(); to lock, GetHAL().lvglUnlock(); to unlock
    // You can also use LvglLockGuard for automatic locking and unlocking
    LvglLockGuard lock;

    // Create a quit button
    // Using lvgl cpp wrapper here
    // Same as using c method directly, lv_button_create...
    _button_quit = std::make_unique<Button>(lv_screen_active());
    _button_quit->setAlign(LV_ALIGN_CENTER);
    _button_quit->label().setText("QUIT");
    _button_quit->onClick().connect([this]() {
        // Call close() to close the App
        close();
    });
}

// Called repeatedly while the App is running
void AppTemplate::onRunning()
{
    // mclog::tagInfo(getAppInfo().name, "on running");

    // Print "hi" every 1 second
    if (GetHAL().millis() - _time_count > 1000) {
        mclog::tagInfo(getAppInfo().name, "hi");
        _time_count = GetHAL().millis();
    }
}

// Called when the App is closed
// You can destroy UI, release resources, etc. here
void AppTemplate::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    // Destroy button
    _button_quit.reset();
}
