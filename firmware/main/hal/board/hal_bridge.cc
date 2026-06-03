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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

static constexpr std::string_view _xiaozhi_config_nvs_ns                           = "xiaozhi";
static constexpr std::string_view _xiaozhi_config_idle_shutdown_time_key           = "idle_sec";
static constexpr std::string_view _xiaozhi_config_allow_shutdown_when_charging_key = "ext_pwr";
static constexpr std::string_view _xiaozhi_config_idle_random_movement_key         = "idle_lv";
static constexpr std::string_view _xiaozhi_config_start_ai_agent_on_boot_key       = "boot_ai";
static constexpr std::string_view _local_control_config_nvs_ns                    = "local_ctrl";
static constexpr std::string_view _local_control_config_token_key                  = "token";

static constexpr const char* _sdcard_settings_dir      = SDCARD_MOUNT_POINT "/stackchan";
static constexpr const char* _sdcard_settings_tmp_path = SDCARD_MOUNT_POINT "/stackchan/settings.tmp";

static const char* _tag = "HAL_BRIDGE";
static bool _boot_xiaozhi_config_loaded = false;
static hal_bridge::XiaozhiConfig_t _boot_xiaozhi_config;

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

static bool has_json_key(cJSON* object, const char* key)
{
    return cJSON_GetObjectItem(object, key) != nullptr;
}

static bool is_json_integer(cJSON* item)
{
    return cJSON_IsNumber(item) && std::fabs(item->valuedouble - static_cast<double>(item->valueint)) < 0.0001;
}

static bool validate_json_bool(cJSON* object, const char* primary_key, const char* fallback_key, std::string& error)
{
    cJSON* item = cJSON_GetObjectItem(object, primary_key);
    if (item == nullptr && fallback_key != nullptr) {
        item = cJSON_GetObjectItem(object, fallback_key);
    }
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsBool(item)) {
        error = std::string("setting must be boolean: ") + primary_key;
        return false;
    }
    return true;
}

static bool validate_json_uint32(cJSON* object, const char* primary_key, const char* fallback_key, uint32_t min_value,
                                 uint32_t max_value, std::string& error)
{
    cJSON* item = cJSON_GetObjectItem(object, primary_key);
    if (item == nullptr && fallback_key != nullptr) {
        item = cJSON_GetObjectItem(object, fallback_key);
    }
    if (item == nullptr) {
        return true;
    }
    if (!is_json_integer(item)) {
        error = std::string("setting must be an integer: ") + primary_key;
        return false;
    }
    if (item->valueint < static_cast<int>(min_value) || item->valueint > static_cast<int>(max_value)) {
        error = std::string("setting out of range: ") + primary_key;
        return false;
    }
    return true;
}

static bool has_xiaozhi_settings_keys(cJSON* object)
{
    return has_json_key(object, "idleShutdownTimeSeconds") || has_json_key(object, _xiaozhi_config_idle_shutdown_time_key.data()) ||
           has_json_key(object, "allowShutdownWhenCharging") ||
           has_json_key(object, _xiaozhi_config_allow_shutdown_when_charging_key.data()) ||
           has_json_key(object, "idleRandomMovementLevel") ||
           has_json_key(object, _xiaozhi_config_idle_random_movement_key.data()) ||
           has_json_key(object, "startAiAgentOnBoot") ||
           has_json_key(object, _xiaozhi_config_start_ai_agent_on_boot_key.data());
}

static bool validate_xiaozhi_settings_object(cJSON* object, std::string& error)
{
    if (!validate_json_uint32(object, "idleShutdownTimeSeconds", _xiaozhi_config_idle_shutdown_time_key.data(), 0,
                              86400, error)) {
        return false;
    }
    if (!validate_json_bool(object, "allowShutdownWhenCharging",
                            _xiaozhi_config_allow_shutdown_when_charging_key.data(), error)) {
        return false;
    }
    if (!validate_json_uint32(object, "idleRandomMovementLevel", _xiaozhi_config_idle_random_movement_key.data(), 0,
                              3, error)) {
        return false;
    }
    if (!validate_json_bool(object, "startAiAgentOnBoot", _xiaozhi_config_start_ai_agent_on_boot_key.data(), error)) {
        return false;
    }
    return true;
}

static bool validate_control_token(const char* token, std::string& error)
{
    if (token == nullptr) {
        error = "localControl.token must be a string";
        return false;
    }

    const size_t len = std::strlen(token);
    if (len < 8 || len > 96) {
        error = "localControl.token length must be 8..96 characters";
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        const char ch = token[i];
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (!ok) {
            error = "localControl.token may only contain letters, numbers, dot, dash, underscore, and tilde";
            return false;
        }
    }

    return true;
}

static cJSON* get_local_control_object(cJSON* root)
{
    cJSON* local_control = cJSON_GetObjectItem(root, "localControl");
    if (cJSON_IsObject(local_control)) {
        return local_control;
    }
    return nullptr;
}

static bool validate_local_control_settings_object(cJSON* object, std::string& error)
{
    cJSON* token = cJSON_GetObjectItem(object, "token");
    if (token == nullptr) {
        return true;
    }
    if (!cJSON_IsString(token)) {
        error = "localControl.token must be a string";
        return false;
    }
    return validate_control_token(token->valuestring, error);
}

static bool validate_settings_root(cJSON* root, std::string& error)
{
    if (!cJSON_IsObject(root)) {
        error = "settings root must be a JSON object";
        return false;
    }

    cJSON* xiaozhi = cJSON_GetObjectItem(root, "xiaozhi");
    if (xiaozhi != nullptr) {
        if (!cJSON_IsObject(xiaozhi)) {
            error = "xiaozhi must be a JSON object";
            return false;
        }
        if (!validate_xiaozhi_settings_object(xiaozhi, error)) {
            return false;
        }
    } else if (has_xiaozhi_settings_keys(root) && !validate_xiaozhi_settings_object(root, error)) {
        return false;
    }

    cJSON* local_control = cJSON_GetObjectItem(root, "localControl");
    if (local_control != nullptr) {
        if (!cJSON_IsObject(local_control)) {
            error = "localControl must be a JSON object";
            return false;
        }
        if (!validate_local_control_settings_object(local_control, error)) {
            return false;
        }
    }

    return true;
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

    std::string error;
    if (!validate_settings_json(settings_json, nullptr, &error)) {
        ESP_LOGW(_tag, "ignored invalid SD settings file: %s", error.c_str());
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

void set_boot_xiaozhi_config(const XiaozhiConfig_t& config)
{
    _boot_xiaozhi_config        = config;
    _boot_xiaozhi_config_loaded = true;
}

bool get_boot_xiaozhi_config(XiaozhiConfig_t& config)
{
    if (!_boot_xiaozhi_config_loaded) {
        return false;
    }

    config = _boot_xiaozhi_config;
    return true;
}

std::string get_local_control_token()
{
    Settings settings(_local_control_config_nvs_ns.data(), false);
    return settings.GetString(_local_control_config_token_key.data(), STACKCHAN_CONTROL_WS_TOKEN);
}

void set_local_control_token(const std::string& token)
{
    Settings settings(_local_control_config_nvs_ns.data(), true);
    settings.SetString(_local_control_config_token_key.data(), token);
}

bool apply_local_control_sd_overrides(std::string& token)
{
    std::string settings_json;
    if (!read_sd_settings_file(settings_json)) {
        return false;
    }

    std::string error;
    if (!validate_settings_json(settings_json, nullptr, &error)) {
        ESP_LOGW(_tag, "ignored invalid SD settings file: %s", error.c_str());
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(settings_json.data(), settings_json.size());
    if (root == nullptr) {
        ESP_LOGW(_tag, "failed to parse SD settings file: %s", SDCARD_SETTINGS_PATH);
        return false;
    }

    cJSON* local_control = get_local_control_object(root);
    cJSON* token_item    = local_control != nullptr ? cJSON_GetObjectItem(local_control, "token") : nullptr;
    if (cJSON_IsString(token_item)) {
        token = token_item->valuestring;
        ESP_LOGI(_tag, "applied local control token override from %s", SDCARD_SETTINGS_PATH);
        cJSON_Delete(root);
        return true;
    }

    cJSON_Delete(root);
    return false;
}

bool read_sd_settings(std::string& contents)
{
    return read_sd_settings_file(contents);
}

bool validate_settings_json(const std::string& contents, std::string* normalized_json, std::string* error_message)
{
    if (contents.empty()) {
        if (error_message != nullptr) {
            *error_message = "settings JSON is empty";
        }
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(contents.data(), contents.size());
    if (root == nullptr) {
        if (error_message != nullptr) {
            *error_message = "settings JSON is not valid JSON";
        }
        return false;
    }

    std::string error;
    const bool valid = validate_settings_root(root, error);
    if (!valid) {
        if (error_message != nullptr) {
            *error_message = error;
        }
        cJSON_Delete(root);
        return false;
    }

    if (normalized_json != nullptr) {
        char* printed = cJSON_Print(root);
        if (printed == nullptr) {
            if (error_message != nullptr) {
                *error_message = "failed to serialize settings JSON";
            }
            cJSON_Delete(root);
            return false;
        }
        normalized_json->assign(printed);
        cJSON_free(printed);
    }

    cJSON_Delete(root);
    return true;
}

bool write_sd_settings(const std::string& contents, std::string* normalized_json, std::string* error_message)
{
    std::string normalized;
    if (!validate_settings_json(contents, &normalized, error_message)) {
        return false;
    }
    normalized.push_back('\n');

    if (mkdir(_sdcard_settings_dir, 0775) != 0 && errno != EEXIST) {
        if (error_message != nullptr) {
            *error_message = std::string("failed to create settings directory: ") + strerror(errno);
        }
        return false;
    }

    FILE* file = fopen(_sdcard_settings_tmp_path, "wb");
    if (file == nullptr) {
        if (error_message != nullptr) {
            *error_message = std::string("failed to open temporary settings file: ") + strerror(errno);
        }
        return false;
    }

    const size_t written = fwrite(normalized.data(), 1, normalized.size(), file);
    const bool failed    = written != normalized.size() || ferror(file);
    if (fclose(file) != 0 || failed) {
        remove(_sdcard_settings_tmp_path);
        if (error_message != nullptr) {
            *error_message = std::string("failed to write settings file: ") + strerror(errno);
        }
        return false;
    }

    remove(SDCARD_SETTINGS_PATH);
    if (rename(_sdcard_settings_tmp_path, SDCARD_SETTINGS_PATH) != 0) {
        remove(_sdcard_settings_tmp_path);
        if (error_message != nullptr) {
            *error_message = std::string("failed to replace settings file: ") + strerror(errno);
        }
        return false;
    }

    if (normalized_json != nullptr) {
        *normalized_json = normalized;
    }
    return true;
}

std::string get_effective_settings_json()
{
    XiaozhiConfig_t xiaozhi_config = get_xiaozhi_config();
    apply_xiaozhi_config_sd_overrides(xiaozhi_config);

    std::string local_control_token = get_local_control_token();
    apply_local_control_sd_overrides(local_control_token);

    cJSON* root    = cJSON_CreateObject();
    cJSON* xiaozhi = cJSON_CreateObject();
    cJSON_AddNumberToObject(xiaozhi, "idleShutdownTimeSeconds", xiaozhi_config.idleShutdownTimeSeconds);
    cJSON_AddBoolToObject(xiaozhi, "allowShutdownWhenCharging", xiaozhi_config.allowShutdownWhenCharging);
    cJSON_AddNumberToObject(xiaozhi, "idleRandomMovementLevel", xiaozhi_config.idleRandomMovementLevel);
    cJSON_AddBoolToObject(xiaozhi, "startAiAgentOnBoot", xiaozhi_config.startAiAgentOnBoot);
    cJSON_AddItemToObject(root, "xiaozhi", xiaozhi);

    cJSON* local_control = cJSON_CreateObject();
    cJSON_AddStringToObject(local_control, "token", local_control_token.c_str());
    cJSON_AddItemToObject(root, "localControl", local_control);

    char* printed = cJSON_PrintUnformatted(root);
    std::string result = printed != nullptr ? printed : "{}";
    if (printed != nullptr) {
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    return result;
}

void app_play_sound(const std::string_view& sound)
{
    auto& app = Application::GetInstance();
    app.PlaySound(sound);
}

}  // namespace hal_bridge
