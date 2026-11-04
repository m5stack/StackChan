// Adapted from ThisIsBenny's StackChan PR #31.
#include "hal_toggles.h"
#include <esp_log.h>
#include <nvs.h>

namespace hal_toggles {
namespace {
constexpr const char* TAG    = "AppVisibility";
constexpr uint8_t VALID_MASK = (1u << TOGGLE_COUNT) - 1;

bool fail(esp_err_t error)
{
    ESP_LOGW(TAG, "Preference storage failed: %s", esp_err_to_name(error));
    return false;
}
}  // namespace

bool load_states(ToggleStates& states)
{
    states = default_states();
    nvs_handle_t handle;
    auto error = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return true;
    if (error != ESP_OK) return fail(error);

    uint8_t mask = VALID_MASK;
    error        = nvs_get_u8(handle, MASK_KEY, &mask);
    auto loaded  = default_states();
    if (error == ESP_OK) {
        if ((mask & ~VALID_MASK) != 0) {
            nvs_close(handle);
            return fail(ESP_ERR_INVALID_STATE);
        }
        for (size_t i = 0; i < TOGGLE_COUNT; ++i) loaded[i] = (mask & (1u << i)) != 0;
    } else if (error == ESP_ERR_NVS_NOT_FOUND) {
        // Migrate the six original keys on the next successful save, without
        // touching other settings or writing merely to open the launcher.
        for (size_t i = 0; i < TOGGLE_COUNT; ++i) {
            uint8_t value = 1;
            error         = nvs_get_u8(handle, TOGGLE_LIST[i].key, &value);
            if (error == ESP_ERR_NVS_NOT_FOUND) continue;
            if (error != ESP_OK || value > 1) {
                nvs_close(handle);
                return fail(error == ESP_OK ? ESP_ERR_INVALID_STATE : error);
            }
            loaded[i] = value != 0;
        }
    } else {
        nvs_close(handle);
        return fail(error);
    }
    nvs_close(handle);
    states = loaded;
    return true;
}

bool save_states(const ToggleStates& states)
{
    nvs_handle_t handle;
    auto error = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return fail(error);

    uint8_t mask = 0;
    for (size_t i = 0; i < TOGGLE_COUNT; ++i) {
        if (states[i]) mask |= 1u << i;
    }
    // One value stores the complete selection, including Reset. Do not leave
    // six independently updated keys after a partially failed reset.
    error = nvs_set_u8(handle, MASK_KEY, mask);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK ? true : fail(error);
}
}  // namespace hal_toggles
