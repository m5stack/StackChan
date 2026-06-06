/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal_bridge.h"
#include "config.h"
#include "stackchan_display.h"
#include <stackchan/avatar/avatar_factory.h>
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

static constexpr std::string_view _ai_agent_config_nvs_ns                           = "agent";
static constexpr std::string_view _ai_agent_config_idle_shutdown_time_key           = "idle_sec";
static constexpr std::string_view _ai_agent_config_allow_shutdown_when_charging_key = "ext_pwr";
static constexpr std::string_view _ai_agent_config_idle_random_movement_key         = "idle_lv";
static constexpr std::string_view _ai_agent_config_start_on_boot_key                = "boot_ai";
static constexpr std::string_view _local_control_config_nvs_ns                    = "local_ctrl";
static constexpr std::string_view _local_control_config_token_key                  = "token";
static constexpr std::string_view _avatar_config_nvs_ns                            = "avatar";
static constexpr std::string_view _avatar_config_skin_key                          = "skin";

static constexpr const char* _sdcard_settings_dir      = SDCARD_MOUNT_POINT "/stackchan";
static constexpr const char* _sdcard_settings_tmp_path = SDCARD_MOUNT_POINT "/stackchan/settings.tmp";

static const char* _tag = "HAL_BRIDGE";
static bool _boot_ai_agent_config_loaded = false;
static hal_bridge::AiAgentConfig_t _boot_ai_agent_config;

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

static bool sdcard_mount_available(std::string* error_message)
{
    struct stat mount_stat = {};
    if (stat(SDCARD_MOUNT_POINT, &mount_stat) != 0) {
        if (error_message != nullptr) {
            *error_message = std::string("SD card is not mounted at ") + SDCARD_MOUNT_POINT + ": " + strerror(errno);
        }
        return false;
    }
    if (!S_ISDIR(mount_stat.st_mode)) {
        if (error_message != nullptr) {
            *error_message = std::string("SD card mount point is not a directory: ") + SDCARD_MOUNT_POINT;
        }
        return false;
    }
    return true;
}

static cJSON* get_ai_agent_override_object(cJSON* root)
{
    cJSON* agent = cJSON_GetObjectItem(root, "agent");
    if (cJSON_IsObject(agent)) {
        return agent;
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

static bool has_ai_agent_settings_keys(cJSON* object)
{
    return has_json_key(object, "idleShutdownTimeSeconds") || has_json_key(object, _ai_agent_config_idle_shutdown_time_key.data()) ||
           has_json_key(object, "allowShutdownWhenCharging") ||
           has_json_key(object, _ai_agent_config_allow_shutdown_when_charging_key.data()) ||
           has_json_key(object, "idleRandomMovementLevel") ||
           has_json_key(object, _ai_agent_config_idle_random_movement_key.data()) ||
           has_json_key(object, "startAiAgentOnBoot") ||
           has_json_key(object, _ai_agent_config_start_on_boot_key.data());
}

static bool validate_ai_agent_settings_object(cJSON* object, std::string& error)
{
    if (!validate_json_uint32(object, "idleShutdownTimeSeconds", _ai_agent_config_idle_shutdown_time_key.data(), 0,
                              86400, error)) {
        return false;
    }
    if (!validate_json_bool(object, "allowShutdownWhenCharging",
                            _ai_agent_config_allow_shutdown_when_charging_key.data(), error)) {
        return false;
    }
    if (!validate_json_uint32(object, "idleRandomMovementLevel", _ai_agent_config_idle_random_movement_key.data(), 0,
                              3, error)) {
        return false;
    }
    if (!validate_json_bool(object, "startAiAgentOnBoot", _ai_agent_config_start_on_boot_key.data(), error)) {
        return false;
    }
    return true;
}

static bool validate_avatar_settings_object(cJSON* object, std::string& error)
{
    cJSON* skin = cJSON_GetObjectItem(object, "skin");
    if (skin == nullptr) {
        skin = cJSON_GetObjectItem(object, _avatar_config_skin_key.data());
    }
    if (skin == nullptr) {
        return true;
    }
    if (!cJSON_IsString(skin)) {
        error = "avatar.skin must be a string";
        return false;
    }

    stackchan::avatar::AvatarSkin parsed_skin;
    if (!stackchan::avatar::parse_avatar_skin(skin->valuestring, parsed_skin)) {
        error = "avatar.skin must be one of: default, ineffa";
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

static bool validate_sync_settings_object(cJSON* object, std::string& error)
{
    cJSON* sync_to_sd = cJSON_GetObjectItem(object, "syncToSd");
    if (sync_to_sd != nullptr && !cJSON_IsBool(sync_to_sd)) {
        error = "settings.syncToSd must be boolean";
        return false;
    }
    return true;
}

static bool validate_settings_root(cJSON* root, std::string& error)
{
    if (!cJSON_IsObject(root)) {
        error = "settings root must be a JSON object";
        return false;
    }

    cJSON* agent = cJSON_GetObjectItem(root, "agent");
    cJSON* agent_object = agent;
    if (agent_object != nullptr) {
        if (!cJSON_IsObject(agent_object)) {
            error = "agent must be a JSON object";
            return false;
        }
        if (!validate_ai_agent_settings_object(agent_object, error)) {
            return false;
        }
    } else if (has_ai_agent_settings_keys(root) && !validate_ai_agent_settings_object(root, error)) {
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

    cJSON* avatar = cJSON_GetObjectItem(root, "avatar");
    if (avatar != nullptr) {
        if (!cJSON_IsObject(avatar)) {
            error = "avatar must be a JSON object";
            return false;
        }
        if (!validate_avatar_settings_object(avatar, error)) {
            return false;
        }
    }

    cJSON* settings = cJSON_GetObjectItem(root, "settings");
    if (settings != nullptr) {
        if (!cJSON_IsObject(settings)) {
            error = "settings must be a JSON object";
            return false;
        }
        if (!validate_sync_settings_object(settings, error)) {
            return false;
        }
    }

    return true;
}

static bool set_settings_sync_to_sd(cJSON* root, bool enabled, std::string* error_message)
{
    cJSON* settings = cJSON_GetObjectItem(root, "settings");
    if (settings == nullptr) {
        settings = cJSON_CreateObject();
        if (settings == nullptr) {
            if (error_message != nullptr) {
                *error_message = "failed to allocate settings object";
            }
            return false;
        }
        cJSON_AddItemToObject(root, "settings", settings);
    } else if (!cJSON_IsObject(settings)) {
        if (error_message != nullptr) {
            *error_message = "settings must be a JSON object";
        }
        return false;
    }

    cJSON_DeleteItemFromObject(settings, "syncToSd");
    if (cJSON_AddBoolToObject(settings, "syncToSd", enabled) == nullptr) {
        if (error_message != nullptr) {
            *error_message = "failed to add settings.syncToSd";
        }
        return false;
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

bool is_agent_mode()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _data.isAgentMode;
}

void set_agent_mode(bool mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _data.isAgentMode = mode;
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

void ai_agent_board_init()
{
    Board::GetInstance();
}

void start_ai_agent_app()
{
    set_agent_mode(true);

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}

AiAgentConfig_t get_ai_agent_config()
{
    AiAgentConfig_t config;

    Settings settings(_ai_agent_config_nvs_ns.data(), false);
    config.idleShutdownTimeSeconds = settings.GetInt(_ai_agent_config_idle_shutdown_time_key.data(),
                                                     static_cast<int>(config.idleShutdownTimeSeconds));
    config.allowShutdownWhenCharging =
        settings.GetBool(_ai_agent_config_allow_shutdown_when_charging_key.data(), config.allowShutdownWhenCharging);
    config.idleRandomMovementLevel =
        settings.GetInt(_ai_agent_config_idle_random_movement_key.data(), config.idleRandomMovementLevel);
    config.startAiAgentOnBoot =
        settings.GetBool(_ai_agent_config_start_on_boot_key.data(), config.startAiAgentOnBoot);

    return config;
}

void set_ai_agent_config(const AiAgentConfig_t& config)
{
    Settings settings(_ai_agent_config_nvs_ns.data(), true);
    settings.SetInt(_ai_agent_config_idle_shutdown_time_key.data(), config.idleShutdownTimeSeconds);
    settings.SetBool(_ai_agent_config_allow_shutdown_when_charging_key.data(), config.allowShutdownWhenCharging);
    settings.SetInt(_ai_agent_config_idle_random_movement_key.data(), config.idleRandomMovementLevel);
    settings.SetBool(_ai_agent_config_start_on_boot_key.data(), config.startAiAgentOnBoot);
}

AvatarConfig_t get_avatar_config()
{
    AvatarConfig_t config;

    Settings settings(_avatar_config_nvs_ns.data(), false);
    const std::string skin = settings.GetString(_avatar_config_skin_key.data(), config.skin);

    stackchan::avatar::AvatarSkin parsed_skin;
    if (stackchan::avatar::parse_avatar_skin(skin, parsed_skin)) {
        config.skin = stackchan::avatar::to_string(parsed_skin);
    }

    return config;
}

void set_avatar_config(const AvatarConfig_t& config)
{
    stackchan::avatar::AvatarSkin parsed_skin;
    if (!stackchan::avatar::parse_avatar_skin(config.skin, parsed_skin)) {
        ESP_LOGW(_tag, "ignored invalid avatar skin: %s", config.skin.c_str());
        return;
    }

    Settings settings(_avatar_config_nvs_ns.data(), true);
    settings.SetString(_avatar_config_skin_key.data(), stackchan::avatar::to_string(parsed_skin));
}

bool apply_ai_agent_config_sd_overrides(AiAgentConfig_t& config)
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

    cJSON* agent = get_ai_agent_override_object(root);
    config.idleShutdownTimeSeconds =
        get_json_uint32(agent, "idleShutdownTimeSeconds", _ai_agent_config_idle_shutdown_time_key.data(),
                        config.idleShutdownTimeSeconds, 0, 86400);
    config.allowShutdownWhenCharging =
        get_json_bool(agent, "allowShutdownWhenCharging", _ai_agent_config_allow_shutdown_when_charging_key.data(),
                      config.allowShutdownWhenCharging);
    config.idleRandomMovementLevel =
        static_cast<uint8_t>(get_json_uint32(agent, "idleRandomMovementLevel",
                                             _ai_agent_config_idle_random_movement_key.data(),
                                             config.idleRandomMovementLevel, 0, 3));
    config.startAiAgentOnBoot =
        get_json_bool(agent, "startAiAgentOnBoot", _ai_agent_config_start_on_boot_key.data(),
                      config.startAiAgentOnBoot);

    cJSON_Delete(root);
    ESP_LOGI(_tag, "applied SD settings overrides from %s", SDCARD_SETTINGS_PATH);
    return true;
}

bool apply_avatar_config_sd_overrides(AvatarConfig_t& config)
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

    cJSON* avatar = cJSON_GetObjectItem(root, "avatar");
    if (cJSON_IsObject(avatar)) {
        cJSON* skin = cJSON_GetObjectItem(avatar, "skin");
        if (skin == nullptr) {
            skin = cJSON_GetObjectItem(avatar, _avatar_config_skin_key.data());
        }
        if (cJSON_IsString(skin)) {
            stackchan::avatar::AvatarSkin parsed_skin;
            if (stackchan::avatar::parse_avatar_skin(skin->valuestring, parsed_skin)) {
                config.skin = stackchan::avatar::to_string(parsed_skin);
            }
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(_tag, "applied avatar SD settings overrides from %s", SDCARD_SETTINGS_PATH);
    return true;
}

void set_boot_ai_agent_config(const AiAgentConfig_t& config)
{
    _boot_ai_agent_config        = config;
    _boot_ai_agent_config_loaded = true;
}

bool get_boot_ai_agent_config(AiAgentConfig_t& config)
{
    if (!_boot_ai_agent_config_loaded) {
        return false;
    }

    config = _boot_ai_agent_config;
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

static bool validate_and_normalize_settings_json(const std::string& contents, std::string* normalized_json,
                                                 std::string* error_message, bool force_sync_to_sd)
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

    if (force_sync_to_sd && !set_settings_sync_to_sd(root, true, error_message)) {
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

bool validate_settings_json(const std::string& contents, std::string* normalized_json, std::string* error_message)
{
    return validate_and_normalize_settings_json(contents, normalized_json, error_message, false);
}

bool write_sd_settings(const std::string& contents, bool sync_to_sd, std::string* normalized_json,
                       std::string* error_message)
{
    std::string normalized;
    if (!validate_and_normalize_settings_json(contents, &normalized, error_message, sync_to_sd)) {
        return false;
    }
    normalized.push_back('\n');

    if (!sdcard_mount_available(error_message)) {
        return false;
    }

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
    AiAgentConfig_t agent_config = get_ai_agent_config();
    apply_ai_agent_config_sd_overrides(agent_config);

    std::string local_control_token = get_local_control_token();
    apply_local_control_sd_overrides(local_control_token);

    AvatarConfig_t avatar_config = get_avatar_config();
    apply_avatar_config_sd_overrides(avatar_config);

    cJSON* root  = cJSON_CreateObject();
    cJSON* agent = cJSON_CreateObject();
    cJSON_AddNumberToObject(agent, "idleShutdownTimeSeconds", agent_config.idleShutdownTimeSeconds);
    cJSON_AddBoolToObject(agent, "allowShutdownWhenCharging", agent_config.allowShutdownWhenCharging);
    cJSON_AddNumberToObject(agent, "idleRandomMovementLevel", agent_config.idleRandomMovementLevel);
    cJSON_AddBoolToObject(agent, "startAiAgentOnBoot", agent_config.startAiAgentOnBoot);
    cJSON_AddItemToObject(root, "agent", agent);

    cJSON* local_control = cJSON_CreateObject();
    cJSON_AddStringToObject(local_control, "token", local_control_token.c_str());
    cJSON_AddItemToObject(root, "localControl", local_control);

    cJSON* avatar = cJSON_CreateObject();
    cJSON_AddStringToObject(avatar, "skin", avatar_config.skin.c_str());
    cJSON_AddItemToObject(root, "avatar", avatar);

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
