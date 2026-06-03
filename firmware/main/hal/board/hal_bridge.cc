/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal_bridge.h"
#include "config.h"
#include "stackchan_display.h"
#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <application.h>
#include <board.h>
#include <display.h>
#include <mutex>
#include <assets.h>
#include <settings.h>
#include <cJSON.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr std::string_view _xiaozhi_config_nvs_ns                           = "xiaozhi";
static constexpr std::string_view _xiaozhi_config_idle_shutdown_time_key           = "idle_sec";
static constexpr std::string_view _xiaozhi_config_allow_shutdown_when_charging_key = "ext_pwr";
static constexpr std::string_view _xiaozhi_config_idle_random_movement_key         = "idle_lv";
static constexpr std::string_view _xiaozhi_config_start_ai_agent_on_boot_key       = "boot_ai";

static const char* _tag = "HAL_BRIDGE";

static bool read_sd_settings_file(std::string& contents)
{
    FILE* file = fopen(SDCARD_SETTINGS_PATH, "rb");
    if (file == nullptr) {
        if (errno == ENOENT) {
            ESP_LOGD(_tag, "no SD settings file at %s", SDCARD_SETTINGS_PATH);
        } else {
            ESP_LOGW(_tag, "failed to open SD settings file %s: errno=%d (%s)", SDCARD_SETTINGS_PATH, errno,
                     strerror(errno));
        }
        return false;
    }

    constexpr size_t max_settings_file_size = 4096;
    std::vector<char> buffer(max_settings_file_size + 1);
    const size_t bytes_read = fread(buffer.data(), 1, buffer.size(), file);
    const bool read_error   = ferror(file);
    fclose(file);

    if (read_error) {
        ESP_LOGW(_tag, "failed to read SD settings file: %s", SDCARD_SETTINGS_PATH);
        return false;
    }
    if (bytes_read > max_settings_file_size) {
        ESP_LOGW(_tag, "SD settings file is too large: %s", SDCARD_SETTINGS_PATH);
        return false;
    }

    contents.assign(buffer.data(), bytes_read);
    return true;
}

static cJSON* get_xiaozhi_override_object(cJSON* root)
{
    cJSON* xiaozhi = cJSON_GetObjectItem(root, "xiaozhi");
    if (cJSON_IsObject(xiaozhi)) {
        return xiaozhi;
    }
    return root;
}

static bool get_json_bool(cJSON* object, const char* primary_key, const char* fallback_key, bool current_value)
{
    cJSON* item = cJSON_GetObjectItem(object, primary_key);
    if (!cJSON_IsBool(item) && fallback_key != nullptr) {
        item = cJSON_GetObjectItem(object, fallback_key);
    }
    if (!cJSON_IsBool(item)) {
        return current_value;
    }
    return cJSON_IsTrue(item);
}

static uint32_t get_json_uint32(cJSON* object, const char* primary_key, const char* fallback_key, uint32_t current_value,
                                uint32_t min_value, uint32_t max_value)
{
    cJSON* item = cJSON_GetObjectItem(object, primary_key);
    if (!cJSON_IsNumber(item) && fallback_key != nullptr) {
        item = cJSON_GetObjectItem(object, fallback_key);
    }
    if (!cJSON_IsNumber(item)) {
        return current_value;
    }
    const double value = item->valuedouble;
    if (value <= static_cast<double>(min_value)) {
        return min_value;
    }
    if (value >= static_cast<double>(max_value)) {
        return max_value;
    }
    return static_cast<uint32_t>(value);
}

namespace hal_bridge {

/* -------------------------------------------------------------------------- */
/*                            State and touch point                           */
/* -------------------------------------------------------------------------- */

static std::mutex _mutex;
static Data_t _data;

void lock()
{
    _mutex.lock();
}

void unlock()
{
    _mutex.unlock();
}

Data_t& get_data()
{
    return _data;
}

void set_touch_point(int num, int x, int y)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _data.touchPoint.num = num;
    _data.touchPoint.x   = x;
    _data.touchPoint.y   = y;
}

TouchPoint_t get_touch_point()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _data.touchPoint;
}

bool is_xiaozhi_mode()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _data.isXiaozhiMode;
}

void set_xiaozhi_mode(bool mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _data.isXiaozhiMode = mode;
}

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */
#define DISPLAY_TYPE StackChanAvatarDisplay

lv_disp_t* display_get_lvgl_display()
{
    auto display = static_cast<DISPLAY_TYPE*>(Board::GetInstance().GetDisplay());
    return display->GetLvglDisplay();
}

void disply_lvgl_lock()
{
    auto display = static_cast<DISPLAY_TYPE*>(Board::GetInstance().GetDisplay());
    display->LvglLock();
}

void disply_lvgl_unlock()
{
    auto display = static_cast<DISPLAY_TYPE*>(Board::GetInstance().GetDisplay());
    display->LvglUnlock();
}

void display_wait_idle()
{
    auto display = static_cast<DISPLAY_TYPE*>(Board::GetInstance().GetDisplay());
    display->WaitForPendingPanelTransfers();
}

/* -------------------------------------------------------------------------- */
/*                                 Application                                */
/* -------------------------------------------------------------------------- */

void xiaozhi_board_init()
{
    // Init board
    auto& board = Board::GetInstance();
}

void start_xiaozhi_app()
{
    set_xiaozhi_mode(true);

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}

XiaozhiConfig_t get_xiaozhi_config()
{
    XiaozhiConfig_t config;

    Settings settings(_xiaozhi_config_nvs_ns.data(), false);
    config.idleShutdownTimeSeconds = settings.GetInt(_xiaozhi_config_idle_shutdown_time_key.data(),
                                                     static_cast<int>(config.idleShutdownTimeSeconds));
    config.allowShutdownWhenCharging =
        settings.GetBool(_xiaozhi_config_allow_shutdown_when_charging_key.data(), config.allowShutdownWhenCharging);
    config.idleRandomMovementLevel =
        settings.GetInt(_xiaozhi_config_idle_random_movement_key.data(), config.idleRandomMovementLevel);
    config.startAiAgentOnBoot =
        settings.GetBool(_xiaozhi_config_start_ai_agent_on_boot_key.data(), config.startAiAgentOnBoot);

    return config;
}

void set_xiaozhi_config(const XiaozhiConfig_t& config)
{
    Settings settings(_xiaozhi_config_nvs_ns.data(), true);
    settings.SetInt(_xiaozhi_config_idle_shutdown_time_key.data(), config.idleShutdownTimeSeconds);
    settings.SetBool(_xiaozhi_config_allow_shutdown_when_charging_key.data(), config.allowShutdownWhenCharging);
    settings.SetInt(_xiaozhi_config_idle_random_movement_key.data(), config.idleRandomMovementLevel);
    settings.SetBool(_xiaozhi_config_start_ai_agent_on_boot_key.data(), config.startAiAgentOnBoot);
}

bool apply_xiaozhi_config_sd_overrides(XiaozhiConfig_t& config)
{
    std::string settings_json;
    if (!read_sd_settings_file(settings_json)) {
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(settings_json.data(), settings_json.size());
    if (root == nullptr) {
        ESP_LOGW(_tag, "failed to parse SD settings file: %s", SDCARD_SETTINGS_PATH);
        return false;
    }

    cJSON* xiaozhi = get_xiaozhi_override_object(root);
    config.idleShutdownTimeSeconds =
        get_json_uint32(xiaozhi, "idleShutdownTimeSeconds", _xiaozhi_config_idle_shutdown_time_key.data(),
                        config.idleShutdownTimeSeconds, 0, 86400);
    config.allowShutdownWhenCharging =
        get_json_bool(xiaozhi, "allowShutdownWhenCharging", _xiaozhi_config_allow_shutdown_when_charging_key.data(),
                      config.allowShutdownWhenCharging);
    config.idleRandomMovementLevel =
        static_cast<uint8_t>(get_json_uint32(xiaozhi, "idleRandomMovementLevel",
                                             _xiaozhi_config_idle_random_movement_key.data(),
                                             config.idleRandomMovementLevel, 0, 3));
    config.startAiAgentOnBoot =
        get_json_bool(xiaozhi, "startAiAgentOnBoot", _xiaozhi_config_start_ai_agent_on_boot_key.data(),
                      config.startAiAgentOnBoot);

    cJSON_Delete(root);
    ESP_LOGI(_tag, "applied SD settings overrides from %s", SDCARD_SETTINGS_PATH);
    return true;
}

void app_play_sound(const std::string_view& sound)
{
    auto& app = Application::GetInstance();
    app.PlaySound(sound);
}

}  // namespace hal_bridge
