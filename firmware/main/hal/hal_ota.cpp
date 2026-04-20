/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <memory>
#include <ota.h>

static const std::string_view _tag = "HAL-OTA";

bool Hal::updateFirmware(std::function<void(std::string_view)> onLog)
{
    onLog("Checking firmware updates...");

    Ota ota;
    esp_err_t err = ota.CheckVersion();
    if (err != ESP_OK) {
        mclog::tagError(_tag, "failed to check firmware version: {}", esp_err_to_name(err));
        onLog("Failed to check firmware updates");
        return false;
    }

    if (!ota.HasNewVersion()) {
        ota.MarkCurrentVersionValid();
        mclog::tagInfo(_tag, "no new firmware version available");
        onLog("Already up to date");
        return true;
    }

    const std::string &firmware_url     = ota.GetFirmwareUrl();
    const std::string &firmware_version = ota.GetFirmwareVersion();
    if (firmware_url.empty()) {
        mclog::tagError(_tag, "firmware update available but url is empty");
        onLog("Invalid firmware update info");
        return false;
    }

    mclog::tagInfo(_tag, "new firmware available: version={}, url={}", firmware_version, firmware_url);
    if (!firmware_version.empty()) {
        onLog(std::string("New firmware found: ") + firmware_version);
    } else {
        onLog("New firmware found");
    }

    onLog("Starting firmware upgrade...");
    bool upgrade_success = Ota::Upgrade(firmware_url, [&](int progress, size_t speed) {
        auto msg = fmt::format("Upgrading firmware: {}% at {}KB/s", progress, speed / 1024);
        mclog::tagInfo(_tag, "upgrade progress: {}", msg);
        onLog(msg);
    });

    if (!upgrade_success) {
        mclog::tagError(_tag, "firmware upgrade failed: version={}, url={}", firmware_version, firmware_url);
        onLog("Firmware upgrade failed, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        reboot();
        return false;
    }

    mclog::tagInfo(_tag, "firmware upgrade successful, rebooting");
    onLog("Upgrade successful, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    reboot();
    return true;
}
