/*
 * SPDX-FileCopyrightText: 2026 GOROman / null-bot
 *
 * SPDX-License-Identifier: MIT
 */

#include "wake_manager.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

static const char* TAG = "WakeManager";

// Cache the wake reason determined at boot so it survives the AW9523
// register read clearing the latch.
static wake_reason_t s_cached_wake_reason = WAKE_REASON_UNKNOWN;
static bool s_cached_valid                = false;

extern "C" wake_reason_t wake_manager_check_boot_reason(void)
{
    if (s_cached_valid) {
        return s_cached_wake_reason;
    }

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "ESP wake cause = %d", (int)cause);

    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0:
            // Source comes from AW9523 INTN. Concrete line identification
            // (P1_2 = Si12T vs. others = FT6336U etc.) is left to the caller
            // who has access to the AW9523 driver (we keep this module free
            // of board-specific I2C dependencies for build-time minimality).
            s_cached_wake_reason = WAKE_REASON_HEAD_TOUCH_SI12T;
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        case ESP_SLEEP_WAKEUP_ALL:
            s_cached_wake_reason = WAKE_REASON_POWER_ON;
            break;
        default:
            s_cached_wake_reason = WAKE_REASON_OTHER_EXT0;
            break;
    }

    s_cached_valid = true;
    return s_cached_wake_reason;
}

extern "C" void wake_manager_enter_deep_sleep(void)
{
    ESP_LOGW(TAG, "Entering deep sleep, wake on GPIO%d (low)",
             (int)WAKE_MANAGER_AW9523_INT_GPIO);

    // ext0 wake on GPIO21 going LOW. AW9523 INTN is active-low.
    // GPIO21 is an RTC IO on ESP32-S3, so ext0 is supported.
    esp_err_t err = esp_sleep_enable_ext0_wakeup(WAKE_MANAGER_AW9523_INT_GPIO, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_sleep_enable_ext0_wakeup failed: 0x%x", err);
    }

    // Hold the GPIO so its pull config is preserved across deep sleep.
    rtc_gpio_pullup_en(WAKE_MANAGER_AW9523_INT_GPIO);
    rtc_gpio_pulldown_dis(WAKE_MANAGER_AW9523_INT_GPIO);

    esp_deep_sleep_start();
    // unreachable
    for (;;) {}
}
