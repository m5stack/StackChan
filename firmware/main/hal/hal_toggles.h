#pragma once
#include <cstdint>
#include <string_view>

namespace hal_toggles {

constexpr const char* NVS_NAMESPACE = "feature_toggles";

constexpr const char* KEY_AI_AGENT = "ai_agent";
constexpr const char* KEY_AVATAR = "avatar";
constexpr const char* KEY_ESPNOW_REMOTE = "espnow_remote";
constexpr const char* KEY_APP_CENTER = "app_center";
constexpr const char* KEY_EZDATA = "ezdata";
constexpr const char* KEY_DANCE = "dance";

constexpr uint8_t TOGGLE_COUNT = 6;

struct ToggleInfo {
    const char* key;
    const char* display_name;
    bool default_value;
};

const ToggleInfo TOGGLE_LIST[TOGGLE_COUNT] = {
    {KEY_AI_AGENT, "AI.AGENT", true},
    {KEY_AVATAR, "AVATAR", true},
    {KEY_ESPNOW_REMOTE, "ESPNOW.REMOTE", true},
    {KEY_APP_CENTER, "APP.CENTER", true},
    {KEY_EZDATA, "EZDATA", true},
    {KEY_DANCE, "DANCE", true},
};

bool get_toggle_state(std::string_view key);
void set_toggle_state(std::string_view key, bool value);
void reset_toggles_to_defaults();
bool is_first_boot();
bool init_nvs();
bool is_app_enabled(std::string_view app_name);

}  // namespace hal_toggles