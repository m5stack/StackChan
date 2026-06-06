/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <mooncake_log.h>
#include <hal/hal.h>
#include <array>
#include <vector>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-AIAgent";

namespace {
static const std::array<const char*, 4> _idle_motion_level_labels = {{"Off", "Low", "Medium", "High"}};
static const std::array<const char*, 2> _avatar_skin_values        = {{"default", "ineffa"}};
constexpr auto kCheckedIndicatorSelector = static_cast<lv_style_selector_t>(
    static_cast<uint32_t>(LV_PART_INDICATOR) | static_cast<uint32_t>(LV_STATE_CHECKED));

}  // namespace

AIAgentPowerSavingWorker::AIAgentPowerSavingWorker()
{
    mclog::info("AIAgentPowerSavingWorker start");

    for (uint32_t seconds = 0; seconds <= 3600; seconds += 300) {
        _idle_shutdown_levels.push_back(seconds);
    }

    _config = GetHAL().getAiAgentConfig();

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
    _switch_charging->setBgColor(lv_color_hex(0x615B9E), kCheckedIndicatorSelector);
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

void AIAgentPowerSavingWorker::update()
{
    if (_pending_idle_index != -1) {
        _config.idleShutdownTimeSeconds = _idle_shutdown_levels[_pending_idle_index];
        _pending_idle_index             = -1;
        update_idle_label();
    }

    if (_confirm_flag) {
        _confirm_flag                     = false;
        _config.allowShutdownWhenCharging = _switch_charging->getValue();
        GetHAL().setAiAgentConfig(_config);
        mclog::tagInfo(_tag, "AI agent config updated: idleShutdownTimeSeconds={}, allowShutdownWhenCharging={}",
                       _config.idleShutdownTimeSeconds, _config.allowShutdownWhenCharging);
        _is_done = true;
    }
}

void AIAgentPowerSavingWorker::update_idle_label()
{
    if (_config.idleShutdownTimeSeconds == 0) {
        _label_idle_value->setText("Off");
        return;
    }

    auto total_minutes = _config.idleShutdownTimeSeconds / 60;
    _label_idle_value->setText(fmt::format("{} min", total_minutes));
}

AIAgentGeneralWorker::AIAgentGeneralWorker()
{
    mclog::info("AIAgentGeneralWorker start");

    _config        = GetHAL().getAiAgentConfig();
    _avatar_config = GetHAL().getAvatarConfig();

    for (uint8_t level = 0; level < _idle_motion_level_labels.size(); ++level) {
        _idle_motion_levels.push_back(level);
    }

    int current_index = static_cast<int>(_idle_motion_levels.size()) - 1;
    for (size_t i = 0; i < _idle_motion_levels.size(); ++i) {
        if (_idle_motion_levels[i] >= _config.idleRandomMovementLevel) {
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

    _panel_general = std::make_unique<Container>(_panel->get());
    _panel_general->setSize(296, 156);
    _panel_general->align(LV_ALIGN_TOP_MID, 0, 20);
    _panel_general->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_general->setBorderWidth(0);
    _panel_general->setRadius(18);
    _panel_general->setPadding(0, 0, 0, 0);
    _panel_general->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_idle_motion_title = std::make_unique<Label>(_panel_general->get());
    _label_idle_motion_title->setText("Idle movement frequency:");
    _label_idle_motion_title->setTextFont(&lv_font_montserrat_16);
    _label_idle_motion_title->setTextColor(lv_color_hex(0x26206A));
    _label_idle_motion_title->setWidth(260);
    _label_idle_motion_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_idle_motion_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _label_idle_motion_value = std::make_unique<Label>(_panel_general->get());
    _label_idle_motion_value->setTextFont(&lv_font_montserrat_24);
    _label_idle_motion_value->setTextColor(lv_color_hex(0x26206A));
    _label_idle_motion_value->align(LV_ALIGN_TOP_MID, 0, 64);

    _slider_idle_motion = std::make_unique<Slider>(_panel_general->get());
    _slider_idle_motion->align(LV_ALIGN_TOP_MID, 0, 118);
    _slider_idle_motion->setRange(0, _idle_motion_levels.size() - 1);
    _slider_idle_motion->setSize(250, 18);
    _slider_idle_motion->setBgColor(lv_color_hex(0x615B9E), LV_PART_KNOB);
    _slider_idle_motion->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR);
    _slider_idle_motion->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _slider_idle_motion->setBgOpa(255);
    _slider_idle_motion->setValue(current_index);
    _slider_idle_motion->onValueChanged().connect([this](int32_t value) { _pending_idle_motion_index = value; });

    _panel_startup = std::make_unique<Container>(_panel->get());
    _panel_startup->setSize(296, 120);
    _panel_startup->align(LV_ALIGN_TOP_MID, 0, 196);
    _panel_startup->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_startup->setBorderWidth(0);
    _panel_startup->setRadius(18);
    _panel_startup->setPadding(0, 0, 0, 0);
    _panel_startup->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_startup_title = std::make_unique<Label>(_panel_startup->get());
    _label_startup_title->setText("Start AI Agent on boot:");
    _label_startup_title->setTextFont(&lv_font_montserrat_16);
    _label_startup_title->setTextColor(lv_color_hex(0x26206A));
    _label_startup_title->setWidth(260);
    _label_startup_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_startup_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _switch_start_ai_on_boot = std::make_unique<Switch>(_panel_startup->get());
    _switch_start_ai_on_boot->setSize(64, 36);
    _switch_start_ai_on_boot->align(LV_ALIGN_TOP_MID, 0, 66);
    _switch_start_ai_on_boot->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _switch_start_ai_on_boot->setBgColor(lv_color_hex(0x615B9E), kCheckedIndicatorSelector);
    _switch_start_ai_on_boot->setBgColor(lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    if (_config.startAiAgentOnBoot) {
        _switch_start_ai_on_boot->addState(LV_STATE_CHECKED);
    }

    _panel_avatar_skin = std::make_unique<Container>(_panel->get());
    _panel_avatar_skin->setSize(296, 162);
    _panel_avatar_skin->align(LV_ALIGN_TOP_MID, 0, 336);
    _panel_avatar_skin->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_avatar_skin->setBorderWidth(0);
    _panel_avatar_skin->setRadius(18);
    _panel_avatar_skin->setPadding(0, 0, 0, 0);
    _panel_avatar_skin->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_avatar_skin_title = std::make_unique<Label>(_panel_avatar_skin->get());
    _label_avatar_skin_title->setText("Avatar face skin:");
    _label_avatar_skin_title->setTextFont(&lv_font_montserrat_16);
    _label_avatar_skin_title->setTextColor(lv_color_hex(0x26206A));
    _label_avatar_skin_title->setWidth(260);
    _label_avatar_skin_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_avatar_skin_title->align(LV_ALIGN_TOP_MID, 0, 14);

    _roller_avatar_skin = std::make_unique<Roller>(_panel_avatar_skin->get());
    _roller_avatar_skin->setSize(180, 66);
    _roller_avatar_skin->setOptions("M5 Default\nIneffa");
    _roller_avatar_skin->align(LV_ALIGN_TOP_MID, 0, 42);
    _roller_avatar_skin->setTextFont(&lv_font_montserrat_16);
    _roller_avatar_skin->setTextColor(lv_color_hex(0x26206A));
    _roller_avatar_skin->setBgColor(lv_color_hex(0xB8D3FD));
    _roller_avatar_skin->setBgColor(lv_color_hex(0x615B9E), LV_PART_SELECTED);
    _roller_avatar_skin->setRadius(12);
    _roller_avatar_skin->setShadowWidth(0);
    _roller_avatar_skin->setBorderWidth(0);
    for (size_t i = 0; i < _avatar_skin_values.size(); ++i) {
        if (_avatar_config.skin == _avatar_skin_values[i]) {
            _roller_avatar_skin->setSelected(i, LV_ANIM_OFF);
            break;
        }
    }

    _label_avatar_skin_note = std::make_unique<Label>(_panel_avatar_skin->get());
    _label_avatar_skin_note->setText("Restart required");
    _label_avatar_skin_note->setTextFont(&lv_font_montserrat_16);
    _label_avatar_skin_note->setTextColor(lv_color_hex(0x615B9E));
    _label_avatar_skin_note->setWidth(260);
    _label_avatar_skin_note->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_avatar_skin_note->align(LV_ALIGN_TOP_MID, 0, 122);

    _btn_confirm = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->align(LV_ALIGN_TOP_MID, 0, 520);
    _btn_confirm->setSize(290, 50);
    _btn_confirm->label().setText("Confirm");
    _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

    update_idle_motion_label();
}

void AIAgentGeneralWorker::update()
{
    if (_pending_idle_motion_index != -1) {
        _config.idleRandomMovementLevel = _idle_motion_levels[_pending_idle_motion_index];
        _pending_idle_motion_index      = -1;
        update_idle_motion_label();
    }

    if (_confirm_flag) {
        _confirm_flag = false;
        _config.startAiAgentOnBoot = _switch_start_ai_on_boot->getValue();
        uint16_t selected_skin      = _roller_avatar_skin->getSelected();
        if (selected_skin < _avatar_skin_values.size()) {
            _avatar_config.skin = _avatar_skin_values[selected_skin];
        }
        GetHAL().setAiAgentConfig(_config);
        GetHAL().setAvatarConfig(_avatar_config);
        mclog::tagInfo(_tag, "AI agent config updated: idleRandomMovementLevel={} ({})", _config.idleRandomMovementLevel,
                       _idle_motion_level_labels[_config.idleRandomMovementLevel]);
        mclog::tagInfo(_tag, "avatar skin config updated: {} (restart required)", _avatar_config.skin);
        _is_done = true;
    }
}

void AIAgentGeneralWorker::update_idle_motion_label()
{
    _label_idle_motion_value->setText(_idle_motion_level_labels[_config.idleRandomMovementLevel]);
}
