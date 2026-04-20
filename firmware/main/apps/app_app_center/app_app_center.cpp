/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_app_center.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <apps/common/common.h>
#include <memory>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppAppCenter::AppAppCenter()
{
    // 配置 App 名
    setAppInfo().name = "APP.CENTER";
    // 配置 App 图标
    static auto icon  = assets::get_image("icon_app_center.bin");
    setAppInfo().icon = (void*)&icon;
    // 配置 App 主题颜色
    static uint32_t theme_color = 0xF4A354;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppAppCenter::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAppCenter::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Create loading page
    std::unique_ptr<view::LoadingPage> loading_page;
    {
        LvglLockGuard lock;
        loading_page = std::make_unique<view::LoadingPage>(0xF4A354, 0x332609);
    }

    // Start network
    GetHAL().startNetwork([&](std::string_view msg) {
        LvglLockGuard lock;
        loading_page->setMessage(msg);
    });

    // Fetch app list
    {
        LvglLockGuard lock;
        loading_page->setMessage("Fetching app list...");
    }
    _app_list = GetHAL().fetchAppList();

    LvglLockGuard lock;

    // Destroy loading page
    loading_page.reset();

    _app_list_page = std::make_unique<view::AppListPage>(_app_list);

    view::create_home_indicator([&]() { close(); }, 0xFFDF9A, 0x47330A);
    view::create_status_bar(0xFFDF9A, 0x47330A);
}

void AppAppCenter::onRunning()
{
    if (_launch_requested) {
        GetHAL().launchApp(_app_list[_selected_index].firmwareUrl, [&](int percent) {
            LvglLockGuard lock;
            if (_app_install_page) {
                _app_install_page->setProgress(percent);
            }
        });
    }

    LvglLockGuard lock;

    if (_app_list_page) {
        _selected_index = _app_list_page->isSelected();
        if (_selected_index >= 0) {
            mclog::tagInfo(getAppInfo().name, "selected index: {}", _selected_index);
            _app_list_page.reset();
            _app_detail_page = std::make_unique<view::AppDetailPage>(_app_list[_selected_index]);
        }
    }

    if (_app_detail_page) {
        if (_app_detail_page->checkBack()) {
            mclog::tagInfo(getAppInfo().name, "back to app list");
            _app_detail_page.reset();
            _app_list_page = std::make_unique<view::AppListPage>(_app_list);
        } else if (_app_detail_page->checkLaunch()) {
            mclog::tagInfo(getAppInfo().name, "launch app");
            _app_detail_page.reset();
            _app_install_page = std::make_unique<view::AppInstallPage>(_app_list[_selected_index]);
            _launch_requested = true;
        }
    }

    view::update_home_indicator();
    view::update_status_bar();
}

void AppAppCenter::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _app_list.clear();
    _app_list_page.reset();
    _app_detail_page.reset();
    _app_install_page.reset();
    _selected_index   = -1;
    _launch_requested = false;

    view::destroy_home_indicator();
    view::destroy_status_bar();

    GetHAL().requestWarmReboot(3);
}
