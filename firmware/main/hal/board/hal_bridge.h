/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "stackchan_camera.h"
#include <cstdint>
#include <lvgl.h>
#include <driver/i2c_master.h>
#include <string>
#include <string_view>

namespace hal_bridge {

struct TouchPoint_t {
    int num = 0;
    int x   = -1;
    int y   = -1;
};

struct Data_t {
    TouchPoint_t touchPoint;
    bool isAgentMode              = false;
    bool isAgentModeToggleEnabled = false;
};

struct AiAgentConfig_t {
    uint32_t idleShutdownTimeSeconds = 600;
    bool allowShutdownWhenCharging   = false;
    uint8_t idleRandomMovementLevel  = 2;
    bool startAiAgentOnBoot          = false;
};

struct AvatarConfig_t {
    std::string skin = "ineffa";
};

void lock();
void unlock();
Data_t& get_data();

void set_touch_point(int num, int x, int y);
TouchPoint_t get_touch_point();

bool is_agent_mode();
void set_agent_mode(bool mode);
void toggle_ai_agent_chat_state();

void disply_lvgl_lock();
void disply_lvgl_unlock();
void display_wait_idle();
lv_disp_t* display_get_lvgl_display();

void ai_agent_board_init();
void start_ai_agent_app();
bool is_ai_agent_ready();
bool is_ai_agent_idle();
AiAgentConfig_t get_ai_agent_config();
void set_ai_agent_config(const AiAgentConfig_t& config);
bool apply_ai_agent_config_sd_overrides(AiAgentConfig_t& config);
AvatarConfig_t get_avatar_config();
void set_avatar_config(const AvatarConfig_t& config);
bool apply_avatar_config_sd_overrides(AvatarConfig_t& config);
void set_boot_ai_agent_config(const AiAgentConfig_t& config);
bool get_boot_ai_agent_config(AiAgentConfig_t& config);
std::string get_local_control_token();
void set_local_control_token(const std::string& token);
bool apply_local_control_sd_overrides(std::string& token);
bool read_sd_settings(std::string& contents);
bool validate_settings_json(const std::string& contents, std::string* normalized_json, std::string* error_message);
bool write_sd_settings(const std::string& contents, bool sync_to_sd, std::string* normalized_json,
                       std::string* error_message);
std::string get_effective_settings_json();

i2c_master_bus_handle_t board_get_i2c_bus();
StackChanCamera* board_get_camera();
int board_get_battery_level();
bool board_is_battery_charging();
void board_set_backlight_brightness(uint8_t brightness, bool permanent = false);
uint8_t board_get_backlight_brightness();
void board_set_speaker_volume(uint8_t volume, bool permanent = false);
uint8_t board_get_speaker_volume();

void app_play_sound(const std::string_view& sound);

}  // namespace hal_bridge
