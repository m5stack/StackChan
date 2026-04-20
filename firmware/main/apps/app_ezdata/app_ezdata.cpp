/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_ezdata.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <stackchan/stackchan.h>

using namespace mooncake;
using namespace stackchan;

AppEzdata::AppEzdata()
{
    // 配置 App 名
    setAppInfo().name = "EZDATA";
    // 配置 App 图标
    static auto icon  = assets::get_image("icon_ezdata.bin");
    setAppInfo().icon = (void*)&icon;
    // 配置 App 主题颜色
    static uint32_t theme_color = 0x60A5FA;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppEzdata::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppEzdata::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    {
        LvglLockGuard lock;

        // Create default avatar
        auto avatar = std::make_unique<avatar::DefaultAvatar>();
        avatar->init(lv_screen_active());
        GetStackChan().attachAvatar(std::move(avatar));

        // Create loading page
        _loading_page = std::make_unique<view::LoadingPage>(0x60A5FA, 0x072448);
    }

    // Start ezdata service
    GetHAL().startEzDataService([&](std::string_view msg) {
        LvglLockGuard lock;
        _loading_page->setMessage(msg);
    });

    LvglLockGuard lock;

    /* --------------------------------- Events --------------------------------- */
    GetHAL().onEzdataPairCode.connect([this](std::string_view code) {
        LvglLockGuard lock;

        // Destroy loading page
        _loading_page.reset();

        // Client connected
        if (code.empty()) {
            _ezdata_guide_page.reset();
            return;
        }

        _ezdata_guide_page = std::make_unique<view::EzdataGuidePage>(code);
    });
    // _ezdata_guide_page = std::make_unique<view::EzdataGuidePage>("941004");

    /* ----------------------------- Common widgets ----------------------------- */
    view::create_home_indicator([&]() { close(); }, 0x93C3FE, 0x072448);
    view::create_status_bar(0x93C3FE, 0x072448);
}

void AppEzdata::onRunning()
{
    LvglLockGuard lock;

    GetStackChan().update();

    view::update_home_indicator();
    view::update_status_bar();
}

void AppEzdata::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    GetStackChan().resetAvatar();

    view::destroy_home_indicator();
    view::destroy_status_bar();

    GetHAL().requestWarmReboot(4);
}
