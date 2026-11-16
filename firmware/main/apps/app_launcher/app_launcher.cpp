/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include <hal/hal.h>
#include <hal/hal_toggles.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <stackchan/stackchan.h>
#include <cstdint>

using namespace mooncake;

void AppLauncher::onLauncherCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    // 打开自己
    open();
}

void AppLauncher::onLauncherOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;

    if (!_startup_checked && GetHAL().consumeAppSetupRequest()) {
        mclog::tagInfo(getAppInfo().name, "resume App setup after network/binding failure");
        _startup_worker = std::make_unique<setup_workers::WifiSetupWorker>();
    } else if (!_startup_checked && !GetHAL().isAppConfiged()) {
        mclog::tagInfo(getAppInfo().name, "app not configured, start startup worker");
        _startup_worker = std::make_unique<setup_workers::StartupWorker>();
    } else {
        create_launcher_view();
    }
}

void AppLauncher::onLauncherRunning()
{
    LvglLockGuard lock;

    if (_startup_worker) {
        _startup_worker->update();
        if (_startup_worker->isDone()) {
            _startup_worker.reset();
            _startup_checked = true;
            create_launcher_view();
        }
    } else {
        _view->update();
        screensaver_update();
    }

    GetStackChan().update();
}

void AppLauncher::onLauncherClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _view.reset();
}

void AppLauncher::onLauncherDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppLauncher::create_launcher_view()
{
    _view = std::make_unique<view::LauncherView>();
    hal_toggles::ToggleStates states;
    if (!hal_toggles::load_states(states)) {
        mclog::tagWarn(getAppInfo().name, "Unable to load menu preferences; showing all apps");
    }
    auto app_props       = getAppProps();
    int restore_app_id   = -1;
    const int warm_index = GetHAL().getWarmRebootTarget();
    if (warm_index >= 0) {
        // Existing callers store an index in the unfiltered app list.
        if (!app_props.empty()) {
            const auto index = uitk::clamp(warm_index, 0, static_cast<int>(app_props.size()) - 1);
            restore_app_id   = app_props[index].appID;
        }
        GetHAL().clearWarmRebootRequest();
    }
    std::vector<mooncake::AppProps_t> filtered_props;
    for (const auto& props : app_props) {
        if (hal_toggles::is_app_enabled(props.appID, states)) {
            filtered_props.push_back(props);
        }
    }
    _view->init(filtered_props, restore_app_id);
    _view->onAppClicked = [&](int appID) {
        mclog::tagInfo(getAppInfo().name, "handle open app, app id: {}", appID);
        openApp(appID);
    };
}

void AppLauncher::screensaver_update()
{
    const uint32_t SCREENSAVER_TIMEOUT_MS = 30000;

    uint32_t idle_time = lv_display_get_inactive_time(NULL);
    if (idle_time >= SCREENSAVER_TIMEOUT_MS) {
        if (!_screensaver) {
            _screensaver = std::make_unique<view::Screensaver>();
            _screensaver->init();
        }
    } else if (_screensaver) {
        _screensaver.reset();
    }

    // Update in 30ms interval
    if (_screensaver && GetHAL().millis() - _screensaver_timecount > 30) {
        _screensaver_timecount = GetHAL().millis();
        _screensaver->update();
    }
}
