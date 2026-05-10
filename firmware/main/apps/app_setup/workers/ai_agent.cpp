/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <mooncake_log.h>
#include <hal/hal.h>
#include <vector>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-AIAgent";

XiaozhiPowerSavingWorker::XiaozhiPowerSavingWorker()
{
    mclog::info("XiaozhiPowerSavingWorker start");

    for (uint32_t seconds = 0; seconds <= 3600; seconds += 300) {
        _idle_shutdown_levels.push_back(seconds);
    }

    _config = GetHAL().getXiaozhiConfig();

    int current_index = static_cast<int>(_idle_shutdown_levels.size()) - 1;
    for (size_t i = 0; i < _idle_shutdown_levels.size(); ++i) {
        if (_idle_shutdown_levels[i] >= _config.idleShutdownTimeSeconds) {
            current_index = static_cast<int>(i);
            break;
        }
    }

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setPadding(0, 50, 24, 18);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    _panel_idle_shutdown = std::make_unique<Container>(_panel->get());
    _panel_idle_shutdown->setSize(296, 148);
    _panel_idle_shutdown->align(LV_ALIGN_TOP_MID, 0, 20);
    _panel_idle_shutdown->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_idle_shutdown->setBorderWidth(0);
    _panel_idle_shutdown->setRadius(18);
    _panel_idle_shutdown->setPadding(0, 0, 0, 0);
    _panel_idle_shutdown->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_idle_title = std::make_unique<Label>(_panel_idle_shutdown->get());
    _label_idle_title->setText("Automatically power off after being idle for:");
    _label_idle_title->setWidth(280);
    _label_idle_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_idle_title->setTextFont(&lv_font_montserrat_16);
    _label_idle_title->setTextColor(lv_color_hex(0x26206A));
    _label_idle_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _label_idle_value = std::make_unique<Label>(_panel_idle_shutdown->get());
    _label_idle_value->setTextFont(&lv_font_montserrat_24);
    _label_idle_value->setTextColor(lv_color_hex(0x26206A));
    _label_idle_value->align(LV_ALIGN_TOP_MID, 0, 64);

    _slider_idle_shutdown = std::make_unique<Slider>(_panel_idle_shutdown->get());
    _slider_idle_shutdown->align(LV_ALIGN_TOP_MID, 0, 106);
    _slider_idle_shutdown->setRange(0, _idle_shutdown_levels.size() - 1);
    _slider_idle_shutdown->setSize(250, 18);
    _slider_idle_shutdown->setBgColor(lv_color_hex(0x615B9E), LV_PART_KNOB);
    _slider_idle_shutdown->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR);
    _slider_idle_shutdown->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _slider_idle_shutdown->setBgOpa(255);
    _slider_idle_shutdown->setValue(current_index);
    _slider_idle_shutdown->onValueChanged().connect([this](int32_t value) { _pending_idle_index = value; });

    _panel_charging = std::make_unique<Container>(_panel->get());
    _panel_charging->setSize(296, 120);
    _panel_charging->align(LV_ALIGN_TOP_MID, 0, 188);
    _panel_charging->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_charging->setBorderWidth(0);
    _panel_charging->setRadius(18);
    _panel_charging->setPadding(0, 0, 0, 0);
    _panel_charging->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_charging_title = std::make_unique<Label>(_panel_charging->get());
    _label_charging_title->setText("Allow auto power off while charging:");
    _label_charging_title->setTextFont(&lv_font_montserrat_16);
    _label_charging_title->setTextColor(lv_color_hex(0x26206A));
    _label_charging_title->setWidth(260);
    _label_charging_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_charging_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _switch_charging = std::make_unique<Switch>(_panel_charging->get());
    _switch_charging->setSize(64, 36);
    _switch_charging->align(LV_ALIGN_TOP_MID, 0, 66);
    _switch_charging->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _switch_charging->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR | LV_STATE_CHECKED);
    _switch_charging->setBgColor(lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    if (_config.allowShutdownWhenCharging) {
        _switch_charging->addState(LV_STATE_CHECKED);
    }

    _btn_confirm = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->align(LV_ALIGN_TOP_MID, 0, 326);
    _btn_confirm->setSize(290, 50);
    _btn_confirm->label().setText("Confirm");
    _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

    update_idle_label();
}

void XiaozhiPowerSavingWorker::update()
{
    if (_pending_idle_index != -1) {
        _config.idleShutdownTimeSeconds = _idle_shutdown_levels[_pending_idle_index];
        _pending_idle_index             = -1;
        update_idle_label();
    }

    if (_confirm_flag) {
        _confirm_flag                     = false;
        _config.allowShutdownWhenCharging = _switch_charging->getValue();
        GetHAL().setXiaozhiConfig(_config);
        mclog::tagInfo(_tag, "xiaozhi config updated: idleShutdownTimeSeconds={}, allowShutdownWhenCharging={}",
                       _config.idleShutdownTimeSeconds, _config.allowShutdownWhenCharging);
        _is_done = true;
    }
}

void XiaozhiPowerSavingWorker::update_idle_label()
{
    if (_config.idleShutdownTimeSeconds == 0) {
        _label_idle_value->setText("Off");
        return;
    }

    auto total_minutes = _config.idleShutdownTimeSeconds / 60;
    _label_idle_value->setText(fmt::format("{} min", total_minutes));
}

static const char* frequency_label(uint8_t preset)
{
    switch (preset) {
        case 0:  return "Sparse";
        case 2:  return "Frequent";
        default: return "Normal";
    }
}

static const char* intensity_label(uint8_t preset)
{
    switch (preset) {
        case 0:  return "Calm";
        case 2:  return "Lively";
        default: return "Normal";
    }
}

IdleBehaviorWorker::IdleBehaviorWorker()
{
    mclog::info("IdleBehaviorWorker start");

    _config = GetHAL().getXiaozhiConfig();

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setPadding(0, 50, 24, 18);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    _panel_enable = std::make_unique<Container>(_panel->get());
    _panel_enable->setSize(296, 120);
    _panel_enable->align(LV_ALIGN_TOP_MID, 0, 20);
    _panel_enable->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_enable->setBorderWidth(0);
    _panel_enable->setRadius(18);
    _panel_enable->setPadding(0, 0, 0, 0);
    _panel_enable->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_enable_title = std::make_unique<Label>(_panel_enable->get());
    _label_enable_title->setText("Idle head motion:");
    _label_enable_title->setTextFont(&lv_font_montserrat_16);
    _label_enable_title->setTextColor(lv_color_hex(0x26206A));
    _label_enable_title->setWidth(260);
    _label_enable_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_enable_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _switch_enable = std::make_unique<Switch>(_panel_enable->get());
    _switch_enable->setSize(64, 36);
    _switch_enable->align(LV_ALIGN_TOP_MID, 0, 66);
    _switch_enable->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _switch_enable->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR | LV_STATE_CHECKED);
    _switch_enable->setBgColor(lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    if (_config.idleMotionEnabled) {
        _switch_enable->addState(LV_STATE_CHECKED);
    }

    _panel_frequency = std::make_unique<Container>(_panel->get());
    _panel_frequency->setSize(296, 148);
    _panel_frequency->align(LV_ALIGN_TOP_MID, 0, 160);
    _panel_frequency->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_frequency->setBorderWidth(0);
    _panel_frequency->setRadius(18);
    _panel_frequency->setPadding(0, 0, 0, 0);
    _panel_frequency->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_frequency_title = std::make_unique<Label>(_panel_frequency->get());
    _label_frequency_title->setText("Frequency");
    _label_frequency_title->setWidth(280);
    _label_frequency_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_frequency_title->setTextFont(&lv_font_montserrat_16);
    _label_frequency_title->setTextColor(lv_color_hex(0x26206A));
    _label_frequency_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _label_frequency_value = std::make_unique<Label>(_panel_frequency->get());
    _label_frequency_value->setTextFont(&lv_font_montserrat_24);
    _label_frequency_value->setTextColor(lv_color_hex(0x26206A));
    _label_frequency_value->align(LV_ALIGN_TOP_MID, 0, 64);

    _slider_frequency = std::make_unique<Slider>(_panel_frequency->get());
    _slider_frequency->align(LV_ALIGN_TOP_MID, 0, 106);
    _slider_frequency->setRange(0, 2);
    _slider_frequency->setSize(250, 18);
    _slider_frequency->setBgColor(lv_color_hex(0x615B9E), LV_PART_KNOB);
    _slider_frequency->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR);
    _slider_frequency->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _slider_frequency->setBgOpa(255);
    _slider_frequency->setValue(_config.idleMotionFrequency);
    _slider_frequency->onValueChanged().connect([this](int32_t value) { _pending_frequency_index = value; });

    _panel_intensity = std::make_unique<Container>(_panel->get());
    _panel_intensity->setSize(296, 148);
    _panel_intensity->align(LV_ALIGN_TOP_MID, 0, 328);
    _panel_intensity->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_intensity->setBorderWidth(0);
    _panel_intensity->setRadius(18);
    _panel_intensity->setPadding(0, 0, 0, 0);
    _panel_intensity->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_intensity_title = std::make_unique<Label>(_panel_intensity->get());
    _label_intensity_title->setText("Intensity");
    _label_intensity_title->setWidth(280);
    _label_intensity_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_intensity_title->setTextFont(&lv_font_montserrat_16);
    _label_intensity_title->setTextColor(lv_color_hex(0x26206A));
    _label_intensity_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _label_intensity_value = std::make_unique<Label>(_panel_intensity->get());
    _label_intensity_value->setTextFont(&lv_font_montserrat_24);
    _label_intensity_value->setTextColor(lv_color_hex(0x26206A));
    _label_intensity_value->align(LV_ALIGN_TOP_MID, 0, 64);

    _slider_intensity = std::make_unique<Slider>(_panel_intensity->get());
    _slider_intensity->align(LV_ALIGN_TOP_MID, 0, 106);
    _slider_intensity->setRange(0, 2);
    _slider_intensity->setSize(250, 18);
    _slider_intensity->setBgColor(lv_color_hex(0x615B9E), LV_PART_KNOB);
    _slider_intensity->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR);
    _slider_intensity->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _slider_intensity->setBgOpa(255);
    _slider_intensity->setValue(_config.idleMotionIntensity);
    _slider_intensity->onValueChanged().connect([this](int32_t value) { _pending_intensity_index = value; });

    _btn_confirm = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->align(LV_ALIGN_TOP_MID, 0, 496);
    _btn_confirm->setSize(290, 50);
    _btn_confirm->label().setText("Confirm");
    _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

    update_frequency_label();
    update_intensity_label();
}

void IdleBehaviorWorker::update()
{
    if (_pending_frequency_index != -1) {
        _config.idleMotionFrequency = static_cast<uint8_t>(_pending_frequency_index);
        _pending_frequency_index    = -1;
        update_frequency_label();
    }

    if (_pending_intensity_index != -1) {
        _config.idleMotionIntensity = static_cast<uint8_t>(_pending_intensity_index);
        _pending_intensity_index    = -1;
        update_intensity_label();
    }

    if (_confirm_flag) {
        _confirm_flag             = false;
        _config.idleMotionEnabled = _switch_enable->getValue();
        GetHAL().setXiaozhiConfig(_config);
        mclog::tagInfo(_tag,
                       "idle motion config updated: enabled={}, frequency={}, intensity={}",
                       _config.idleMotionEnabled,
                       _config.idleMotionFrequency,
                       _config.idleMotionIntensity);
        _is_done = true;
    }
}

void IdleBehaviorWorker::update_frequency_label()
{
    _label_frequency_value->setText(frequency_label(_config.idleMotionFrequency));
}

void IdleBehaviorWorker::update_intensity_label()
{
    _label_intensity_value->setText(intensity_label(_config.idleMotionIntensity));
}
