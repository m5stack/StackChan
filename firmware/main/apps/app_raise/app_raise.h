/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <smooth_lvgl.hpp>
#include <cstdint>
#include <memory>
#include <string>

class AppRaise : public mooncake::AppAbility {
public:
    AppRaise();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    struct RaiseState {
        int love   = 50;
        int energy = 80;
        int hunger = 20;
    };

    RaiseState _state;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _status_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _hint_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _button_feed;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _button_play;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _button_pet;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _button_rest;
    uint32_t _last_decay_tick = 0;
    uint32_t _last_save_tick  = 0;
    bool _is_dirty            = false;

    void load_state();
    void save_state();
    void create_ui();
    void update_status_view();
    void handle_feed();
    void handle_play();
    void handle_pet();
    void handle_rest();
    void apply_mood();
    void mark_dirty();
};
