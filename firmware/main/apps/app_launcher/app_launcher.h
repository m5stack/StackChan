/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <apps/app_setup/workers/workers.h>
#include <mooncake.h>
#include <mooncake_templates.h>
#include <cstdint>
#include <memory>

class AppLauncher : public mooncake::templates::AppLauncherBase {
public:
    void onLauncherCreate() override;
    void onLauncherOpen() override;
    void onLauncherRunning() override;
    void onLauncherClose() override;
    void onLauncherDestroy() override;

private:
    std::unique_ptr<view::LauncherView> _view;
    std::unique_ptr<view::Screensaver> _screensaver;
    std::unique_ptr<setup_workers::StartupWorker> _startup_worker;
    uint32_t _screensaver_timecount = 0;
    bool _startup_checked           = false;

    void create_launcher_view();
    void screensaver_update();
};
