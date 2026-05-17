/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_atlas_agent.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppAtlasAgent::AppAtlasAgent()
{
    setAppInfo().name = "ATLAS";
    static auto icon  = assets::get_image("icon_ai_agent.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t theme_color = 0x3366FF;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppAtlasAgent::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAtlasAgent::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // For the first Atlas iteration, reuse the existing Xiaozhi assistant runtime
    // and let backend/provisioning determine whether the active profile is Atlas.
    GetHAL().requestXiaozhiStart();
}

void AppAtlasAgent::onRunning()
{
}

void AppAtlasAgent::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}
