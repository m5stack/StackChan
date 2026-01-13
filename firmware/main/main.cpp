/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install apps
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppAiAgent>());
    GetMooncake().installApp(std::make_unique<AppAvatar>());
    GetMooncake().installApp(std::make_unique<AppEspnowControl>());
    GetMooncake().installApp(std::make_unique<AppSetup>());

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetHAL().updateHeapStatusLog();

        GetMooncake().update();

        if (GetHAL().isXiaozhiStartRequested()) {
            break;
        }
    }

    // Uninstall all apps and destroy mooncake
    GetMooncake().uninstallAllApps();
    DestroyMooncake();

    // Start xiaozhi, never returns
    GetHAL().startXiaozhi();
}
