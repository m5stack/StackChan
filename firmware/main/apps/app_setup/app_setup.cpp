/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setup.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

AppSetup::AppSetup()
{
    // 配置 App 名
    setAppInfo().name = "SETUP";
    // 配置 App 图标
    setAppInfo().icon = (void*)&icon_setup;
    // 配置 App 主题颜色
    static uint32_t theme_color = 0xB3B3B3;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    // open();
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;

    _menu_sections = {{
                          "Connectivity",
                          {
                            // {"Set Up Wi-Fi",
                            // [&]() {
                            //     _destroy_menu = true;
                            //     _worker       = std::make_unique<WifiSetupWorker>();
                            // }},
                           {"App Bind Code",
                            [&]() {
                                _destroy_menu = true;
                                _worker       = std::make_unique<AppBindCodeWorker>();
                            }}},
                      },
                      {
                          "Servo",
                          {{"Zero Calibration",
                            [&]() {
                                _destroy_menu = true;
                                _worker       = std::make_unique<ZeroCalibrationWorker>();
                            }},
                           {"LED Strips Test",
                            [&]() {
                                _destroy_menu = true;
                                _worker       = std::make_unique<RgbTestWorker>();
                            }}},
                      },
                      {
                          "About",
                          {{fmt::format("FW Version:  {}", common::FirmwareVersion), nullptr}},
                      },
                      {
                          "End",
                          {{"Quit", [&]() { close(); }}},
                      }};

    _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
}

void AppSetup::onRunning()
{
    LvglLockGuard lock;

    if (_menu_page) {
        _menu_page->update();
    }

    if (_destroy_menu) {
        _menu_page.reset();
        _destroy_menu = false;
    }

    if (_worker) {
        _worker->update();
        if (_worker->isDone()) {
            _worker.reset();
            _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
        }
    }

    GetStackChan().update();
}

void AppSetup::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _menu_page.reset();
}
