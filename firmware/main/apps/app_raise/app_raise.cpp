/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_raise.h"
#include <apps/common/common.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <settings.h>
#include <stackchan/stackchan.h>
#include <algorithm>
#include <cstdio>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan;

static constexpr const char* kSettingsNs = "raise_app";

static int clamp_stat(int value)
{
    return std::clamp(value, 0, 100);
}

AppRaise::AppRaise()
{
    setAppInfo().name = "RAISE";
    static auto icon  = assets::get_image("icon_bell.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t theme_color = 0x62C1A7;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppRaise::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppRaise::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    load_state();

    LvglLockGuard lock;

    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active());
    GetStackChan().attachAvatar(std::move(avatar));

    create_ui();
    apply_mood();

    view::create_home_indicator([&]() { close(); }, 0x62C1A7, 0x19443A);

    _last_decay_tick = GetHAL().millis();
    _last_save_tick  = GetHAL().millis();
}

void AppRaise::onRunning()
{
    auto now = GetHAL().millis();

    LvglLockGuard lock;

    if (now - _last_decay_tick >= 15000) {
        _last_decay_tick = now;
        _state.hunger    = clamp_stat(_state.hunger + 1);
        _state.energy    = clamp_stat(_state.energy - 1);
        if (_state.hunger >= 85) {
            _state.love = clamp_stat(_state.love - 1);
        }
        mark_dirty();
        update_status_view();
        apply_mood();
    }

    if (_is_dirty && now - _last_save_tick >= 5000) {
        save_state();
    }

    GetStackChan().update();
    view::update_home_indicator();
}

void AppRaise::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    save_state();

    LvglLockGuard lock;

    GetStackChan().resetAvatar();
    view::destroy_home_indicator();

    _button_feed.reset();
    _button_play.reset();
    _button_pet.reset();
    _button_rest.reset();
    _hint_label.reset();
    _status_label.reset();
    _status_panel.reset();
}

void AppRaise::load_state()
{
    Settings settings(kSettingsNs, false);
    _state.love   = clamp_stat(settings.GetInt("love", _state.love));
    _state.energy = clamp_stat(settings.GetInt("energy", _state.energy));
    _state.hunger = clamp_stat(settings.GetInt("hunger", _state.hunger));
}

void AppRaise::save_state()
{
    if (!_is_dirty) {
        return;
    }

    Settings settings(kSettingsNs, true);
    settings.SetInt("love", _state.love);
    settings.SetInt("energy", _state.energy);
    settings.SetInt("hunger", _state.hunger);

    _last_save_tick = GetHAL().millis();
    _is_dirty       = false;
}

void AppRaise::create_ui()
{
    ScreenActive screen;
    screen.removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _status_panel = std::make_unique<Container>(lv_screen_active());
    _status_panel->setSize(130, 78);
    _status_panel->align(LV_ALIGN_TOP_LEFT, 8, 8);
    _status_panel->setRadius(8);
    _status_panel->setBorderWidth(0);
    _status_panel->setBgColor(lv_color_hex(0xFFFFFF));
    _status_panel->setOpa(220);
    _status_panel->setPaddingAll(8);

    _status_label = std::make_unique<Label>(_status_panel->get());
    _status_label->setTextFont(&lv_font_montserrat_16);
    _status_label->setTextColor(lv_color_hex(0x19443A));
    _status_label->align(LV_ALIGN_TOP_LEFT, 0, 0);

    _hint_label = std::make_unique<Label>(lv_screen_active());
    _hint_label->setTextFont(&lv_font_montserrat_16);
    _hint_label->setTextColor(lv_color_hex(0x19443A));
    _hint_label->align(LV_ALIGN_TOP_RIGHT, -8, 12);
    _hint_label->setText("Hello!");

    auto setup_button = [](Button& button, const char* text, int x) {
        button.setSize(72, 38);
        button.align(LV_ALIGN_BOTTOM_LEFT, x, -8);
        button.setBgColor(lv_color_hex(0x62C1A7));
        button.setRadius(8);
        button.label().setText(text);
        button.label().setTextFont(&lv_font_montserrat_16);
    };

    _button_feed = std::make_unique<Button>(lv_screen_active());
    setup_button(*_button_feed, "Feed", 8);
    _button_feed->onClick().connect([this]() { handle_feed(); });

    _button_play = std::make_unique<Button>(lv_screen_active());
    setup_button(*_button_play, "Play", 84);
    _button_play->onClick().connect([this]() { handle_play(); });

    _button_pet = std::make_unique<Button>(lv_screen_active());
    setup_button(*_button_pet, "Pet", 160);
    _button_pet->onClick().connect([this]() { handle_pet(); });

    _button_rest = std::make_unique<Button>(lv_screen_active());
    setup_button(*_button_rest, "Rest", 236);
    _button_rest->onClick().connect([this]() { handle_rest(); });

    update_status_view();
}

void AppRaise::update_status_view()
{
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "Love %d\nEnergy %d\nHungry %d", _state.love, _state.energy, _state.hunger);
    _status_label->setText(buffer);
}

void AppRaise::handle_feed()
{
    _state.hunger = clamp_stat(_state.hunger - 18);
    _state.love   = clamp_stat(_state.love + 2);
    _hint_label->setText("Yummy");
    GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 1800));
    mark_dirty();
    update_status_view();
}

void AppRaise::handle_play()
{
    _state.love   = clamp_stat(_state.love + 6);
    _state.energy = clamp_stat(_state.energy - 12);
    _state.hunger = clamp_stat(_state.hunger + 8);
    _hint_label->setText("Fun!");
    GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 1800));
    mark_dirty();
    update_status_view();
}

void AppRaise::handle_pet()
{
    _state.love = clamp_stat(_state.love + 4);
    _hint_label->setText("Happy");
    GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 1800));
    mark_dirty();
    update_status_view();
}

void AppRaise::handle_rest()
{
    _state.energy = clamp_stat(_state.energy + 18);
    _state.hunger = clamp_stat(_state.hunger + 3);
    _hint_label->setText("Resting");
    GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Sleepy, 1800));
    mark_dirty();
    update_status_view();
}

void AppRaise::apply_mood()
{
    if (!GetStackChan().hasAvatar()) {
        return;
    }

    if (_state.energy <= 20) {
        GetStackChan().avatar().setEmotion(avatar::Emotion::Sleepy);
    } else if (_state.hunger >= 80) {
        GetStackChan().avatar().setEmotion(avatar::Emotion::Sad);
    } else if (_state.love >= 75) {
        GetStackChan().avatar().setEmotion(avatar::Emotion::Happy);
    } else {
        GetStackChan().avatar().setEmotion(avatar::Emotion::Neutral);
    }
}

void AppRaise::mark_dirty()
{
    _is_dirty = true;
}
