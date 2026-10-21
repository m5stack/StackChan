#include "hal_toggles.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <cstring>

static const char* TAG = "hal_toggles";

namespace hal_toggles {

static bool _initialized = false;

bool init_nvs()
{
    if (_initialized) {
        return true;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS init failed after erase: %s", esp_err_to_name(err));
            return false;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }
    _initialized = true;
    ESP_LOGI(TAG, "NVS toggles initialized");
    return true;
}

bool is_first_boot()
{
    init_nvs();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return true;
    }
    nvs_close(handle);
    return false;
}

bool get_toggle_state(std::string_view key)
{
    init_nvs();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS, returning default true for %.*s", (int)key.size(), key.data());
        return true;
    }

    uint8_t value = 1;
    err = nvs_get_u8(handle, key.data(), &value);
    if (err != ESP_OK) {
        value = 1;
    }
    nvs_close(handle);
    return value != 0;
}

void set_toggle_state(std::string_view key, bool value)
{
    init_nvs();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write");
        return;
    }
    err = nvs_set_u8(handle, key.data(), value ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set toggle %.*s", (int)key.size(), key.data());
    }
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Set toggle %.*s = %s", (int)key.size(), key.data(), value ? "ON" : "OFF");
}

void reset_toggles_to_defaults()
{
    init_nvs();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for reset");
        return;
    }

    for (int i = 0; i < TOGGLE_COUNT; i++) {
        err = nvs_set_u8(handle, TOGGLE_LIST[i].key, TOGGLE_LIST[i].default_value ? 1 : 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset toggle %s", TOGGLE_LIST[i].key);
        }
    }
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "All toggles reset to defaults");
}

bool is_app_enabled(std::string_view app_name)
{
    for (int i = 0; i < TOGGLE_COUNT; i++) {
        if (app_name == TOGGLE_LIST[i].display_name) {
            return get_toggle_state(TOGGLE_LIST[i].key);
        }
    }
    return true;
}

}  // namespace hal_toggles