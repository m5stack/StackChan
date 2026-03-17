/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <stackchan/stackchan.h>
#include <apps/common/toast/toast.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <vector>
#include <string>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-System";

struct TimezoneOption_t {
    std::string name;
    std::string tz_posix;
};

static const std::vector<TimezoneOption_t> _timezone_list = {
    {"Baker Is. (UTC-12)", "BIT12"},  {"Midway Island (UTC-11)", "SST11"}, {"Honolulu (UTC-10)", "HST10"},
    {"Alaska (UTC-9)", "AKST9"},      {"Los Angeles (UTC-8)", "PST8"},     {"Denver (UTC-7)", "MST7"},
    {"Chicago (UTC-6)", "CST6"},      {"New York (UTC-5)", "EST5"},        {"Halifax (UTC-4)", "AST4"},
    {"S.Paulo (UTC-3)", "BRT3"},      {"S.Georgia (UTC-2)", "GST2"},       {"Azores (UTC-1)", "AZOT1"},
    {"London (UTC+0)", "GMT0"},       {"Berlin (UTC+1)", "CET-1"},         {"Cairo (UTC+2)", "EET-2"},
    {"Moscow (UTC+3)", "MSK-3"},      {"Dubai (UTC+4)", "GST-4"},          {"Karachi (UTC+5)", "PKT-5"},
    {"Dhaka (UTC+6)", "BST-6"},       {"Bangkok (UTC+7)", "ICT-7"},        {"Beijing (UTC+8)", "CST-8"},
    {"Tokyo (UTC+9)", "JST-9"},       {"Sydney (UTC+10)", "AEST-10"},      {"Noumea (UTC+11)", "SBT-11"},
    {"Auckland (UTC+12)", "NZST-12"}, {"Fiji (UTC+13)", "FJT-13"},         {"Line Islands (UTC+14)", "LINT-14"}};

TimezoneWorker::TimezoneWorker()
{
    _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
    _panel->setPadding(0, 0, 0, 0);
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);

    _label = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
    _label->setText("Time Zone");
    _label->setTextFont(&lv_font_montserrat_16);
    _label->setTextColor(lv_color_hex(0x26206A));
    _label->align(LV_ALIGN_CENTER, 0, -100);

    // Timezone list
    std::string options;
    for (const auto& tz : _timezone_list) {
        options += tz.name + "\n";
    }
    // Remove last newline
    if (!options.empty()) {
        options.pop_back();
    }

    _roller = std::make_unique<uitk::lvgl_cpp::Roller>(_panel->get());
    _roller->setSize(210, 188);
    _roller->setOptions(options.c_str());
    _roller->align(LV_ALIGN_CENTER, -40, 16);
    _roller->setTextFont(&lv_font_montserrat_16);
    _roller->setTextColor(lv_color_hex(0x26206A));
    _roller->setBgColor(lv_color_hex(0xB8D3FD));
    _roller->setRadius(18);
    _roller->setShadowWidth(0);
    _roller->setBorderWidth(0);
    _roller->setBgColor(lv_color_hex(0x615B9E), LV_PART_SELECTED);

    // Set current selection
    std::string current_tz = GetHAL().getTimezone();
    int utc0_index         = -1;
    for (size_t i = 0; i < _timezone_list.size(); ++i) {
        if (current_tz == _timezone_list[i].tz_posix) {
            _roller->setSelected(i, LV_ANIM_OFF);
            utc0_index = -1;
            break;
        } else if (_timezone_list[i].tz_posix == "GMT0") {
            utc0_index = i;
        }
    }

    if (utc0_index >= 0) {
        // Default to UTC+0
        _roller->setSelected(utc0_index, LV_ANIM_OFF);
    }

    _btn_confirm = std::make_unique<uitk::lvgl_cpp::Button>(_panel->get());
    _btn_confirm->label().setText("ok");
    _btn_confirm->label().setTextFont(&lv_font_montserrat_24);
    _btn_confirm->setSize(60, 110);
    _btn_confirm->align(LV_ALIGN_CENTER, 115, 40);
    _btn_confirm->onClick().connect([&]() { _confirm_flag = true; });
    _btn_confirm->setRadius(18);
    _btn_confirm->setShadowWidth(0);
    _btn_confirm->setBgColor(lv_color_hex(0x615B9E));
}

TimezoneWorker::~TimezoneWorker()
{
}

void TimezoneWorker::update()
{
    if (_confirm_flag) {
        _confirm_flag = false;

        uint16_t selected_id = _roller->getSelected();
        if (selected_id < _timezone_list.size()) {
            const auto& selected_option = _timezone_list[selected_id];
            GetHAL().setTimezone(selected_option.tz_posix);

            view::pop_a_toast("Timezone Set", view::ToastType::Success);
            mclog::tagInfo(_tag, "timezone set to: {}", selected_option.name);
        }

        _is_done = true;
    }
}

FactoryResetWorker::FactoryResetWorker()
{
    _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
    _panel->setPadding(0, 0, 0, 0);
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);

    // Title
    _label_title = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
    _label_title->setText("Factory Reset");
    _label_title->setTextFont(&lv_font_montserrat_24);
    _label_title->setTextColor(lv_color_hex(0x26206A));
    _label_title->align(LV_ALIGN_CENTER, 0, -80);

    // Info
    _label_info = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
    _label_info->setTextFont(&lv_font_montserrat_16);
    _label_info->setTextColor(lv_color_hex(0x26206A));
    _label_info->align(LV_ALIGN_CENTER, 0, -20);
    _label_info->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_info->setWidth(280);

    // Cancel Button
    _btn_cancel = std::make_unique<uitk::lvgl_cpp::Button>(_panel->get());
    apply_button_common_style(*_btn_cancel);
    _btn_cancel->align(LV_ALIGN_CENTER, -72, 60);
    _btn_cancel->setSize(112, 48);
    _btn_cancel->label().setText("Cancel");
    _btn_cancel->label().setTextFont(&lv_font_montserrat_20);
    _btn_cancel->onClick().connect([this]() { _cancel_flag = true; });

    // Confirm Button
    _btn_confirm = std::make_unique<uitk::lvgl_cpp::Button>(_panel->get());
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->align(LV_ALIGN_CENTER, 72, 60);
    _btn_confirm->setSize(112, 48);
    _btn_confirm->label().setText("Confirm");
    _btn_confirm->label().setTextFont(&lv_font_montserrat_20);
    _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

    update_ui();
}

FactoryResetWorker::~FactoryResetWorker()
{
}

void FactoryResetWorker::update()
{
    if (_cancel_flag) {
        _is_done = true;
        return;
    }

    if (_confirm_flag) {
        _confirm_flag = false;
        _confirm_count++;

        if (_confirm_count >= 3) {
            mclog::tagInfo(_tag, "factory reset triggered");

            _btn_cancel.reset();
            _btn_confirm.reset();
            _label_title.reset();

            _label_info->setText("Factory Resetting...\nDo not turn off power.");
            _label_info->align(LV_ALIGN_CENTER, 0, 0);

            GetHAL().lvglUnlock();
            GetHAL().delay(200);
            GetHAL().factoryReset();

            while (1) {
                GetHAL().delay(200);
            }

        } else {
            update_ui();
        }
    }
}

void FactoryResetWorker::update_ui()
{
    if (_confirm_count == 0) {
        _label_info->setText("Reset all settings to factory default?\nThis cannot be undone.");
        _btn_confirm->label().setText("Reset");
        _btn_confirm->setBgColor(lv_color_hex(0xFFB8B8));
    } else if (_confirm_count == 1) {
        _label_info->setText("Are you absolutely sure?\nAll user data will be lost!");
        _btn_confirm->label().setText("Yes");
        _btn_confirm->setBgColor(lv_color_hex(0xFF8888));
    } else if (_confirm_count == 2) {
        _label_info->setText("Last Warning!\nPress Confirm to erase everything.");
        _btn_confirm->label().setText("Confirm");
        _btn_confirm->setBgColor(lv_color_hex(0xFF4444));
    }
}
