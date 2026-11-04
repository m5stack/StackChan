/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <settings.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

// Network startup and voice activation may run after Mooncake has been destroyed.
// Reboot into the existing App setup UI rather than creating UI in those tasks.
[[noreturn]] void Hal::requestAppSetup()
{
    // A deliberate reboot for provisioning must not roll back a working OTA
    // image merely because credentials or App binding are missing.
    const auto* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    }
    {
        Settings settings("warm_boot", true);
        settings.SetInt("app_setup", 1);
        settings.SetInt("app_index", 6);
    }
    esp_restart();
}

bool Hal::consumeAppSetupRequest()
{
    Settings settings("warm_boot", true);
    const bool requested = settings.GetInt("app_setup", 0) != 0;
    if (requested) {
        settings.SetInt("app_setup", 0);
    }
    return requested;
}
