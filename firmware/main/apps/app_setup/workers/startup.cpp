/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <stackchan/stackchan.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <hal/hal.h>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-Startup";

StartupZeroCalibrationWorker::StartupZeroCalibrationWorker()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xF6F6F6));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);

    _title = std::make_unique<Label>(lv_screen_active());
    _title->setTextFont(&lv_font_montserrat_20);
    _title->setTextColor(lv_color_hex(0x7E7B9C));
    _title->align(LV_ALIGN_TOP_MID, 0, 13);
    _title->setText("SERVO CALIBRATION");

    _img = std::make_unique<Image>(lv_screen_active());
    _img->setSrc(&setup_stackchan_front_view);
    _img->align(LV_ALIGN_CENTER, -74, 15);

    _btn_next = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_next);
    _btn_next->align(LV_ALIGN_CENTER, 79, 73);
    _btn_next->setSize(112, 48);
    _btn_next->label().setText("Confirm");
    _btn_next->label().setTextFont(&lv_font_montserrat_20);
    _btn_next->onClick().connect([this]() { _is_done = true; });

    _info = std::make_unique<Label>(lv_screen_active());
    _info->setTextFont(&lv_font_montserrat_20);
    _info->setTextColor(lv_color_hex(0x26206A));
    _info->align(LV_ALIGN_TOP_LEFT, 185, 56);
    _info->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _info->setText("Move\nStackChan\nto Home\nposition");
}

StartupZeroCalibrationWorker::~StartupZeroCalibrationWorker()
{
    mclog::tagInfo(_tag, "set current angle as zero");

    auto& motion = GetStackChan().motion();
    motion.yawServo().setCurrentAngleAsZero();
    motion.pitchServo().setCurrentAngleAsZero();
}

StartupWorker::PageStartup::PageStartup()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xF6F6F6));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);

    _btn_skip = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_skip);
    _btn_skip->align(LV_ALIGN_CENTER, -72, 67);
    _btn_skip->setSize(112, 48);
    _btn_skip->setBgColor(lv_color_hex(0xD4D9E0));
    _btn_skip->label().setText("Skip");
    _btn_skip->label().setTextFont(&lv_font_montserrat_20);
    _btn_skip->label().setTextColor(lv_color_hex(0x525064));
    _btn_skip->onClick().connect([this]() { _is_skip_clicked = true; });

    _btn_start = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_start);
    _btn_start->align(LV_ALIGN_CENTER, 72, 67);
    _btn_start->setSize(112, 48);
    _btn_start->label().setText("Start");
    _btn_start->label().setTextFont(&lv_font_montserrat_20);
    _btn_start->onClick().connect([this]() { _is_start_clicked = true; });

    _info = std::make_unique<Label>(lv_screen_active());
    _info->setTextFont(&lv_font_montserrat_24);
    _info->setTextColor(lv_color_hex(0x26206A));
    _info->align(LV_ALIGN_CENTER, 0, -30);
    _info->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _info->setText("Welcome!\nLet's get started.");
}

StartupWorker::StartupWorker()
{
    _page_startup = std::make_unique<PageStartup>();
}

StartupWorker::~StartupWorker()
{
}

void StartupWorker::update()
{
    // Startup page
    if (_page_startup) {
        if (_page_startup->isSkipClicked()) {
            mclog::tagInfo(_tag, "startup skipped");
            _is_done = true;
        } else if (_page_startup->isStartClicked()) {
            _page_startup.reset();
            mclog::tagInfo(_tag, "start servo zero calibration");
            _worker_servo = std::make_unique<StartupZeroCalibrationWorker>();
        }
    }
    // Servo zero calibration page
    else if (_worker_servo) {
        _worker_servo->update();
        if (_worker_servo->isDone()) {
            _worker_servo.reset();
            mclog::tagInfo(_tag, "start wifi setup");
            _worker_wifi = std::make_unique<WifiSetupWorker>();
        }
    }
    // App setup
    else if (_worker_wifi) {
        _worker_wifi->update();
        if (_worker_wifi->isDone()) {
            _worker_wifi.reset();
            mclog::tagInfo(_tag, "startup done");
            _is_done = true;
        }
    }
}
