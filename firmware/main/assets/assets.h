/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <string_view>

LV_FONT_DECLARE(MontserratSemiBold26);

LV_IMG_DECLARE(icon_ai_agent);
LV_IMG_DECLARE(icon_controller);
LV_IMG_DECLARE(icon_indicator_left);
LV_IMG_DECLARE(icon_indicator_right);
LV_IMG_DECLARE(icon_sentinel);
LV_IMG_DECLARE(icon_setup);
LV_IMG_DECLARE(icon_uiflow);
LV_IMG_DECLARE(icon_calibrate);
LV_IMG_DECLARE(icon_remote);
LV_IMG_DECLARE(setup_stackchan_front_view);
LV_IMG_DECLARE(icon_home);

extern const char ogg_camera_shutter_start[] asm("_binary_camera_shutter_ogg_start");
extern const char ogg_camera_shutter_end[] asm("_binary_camera_shutter_ogg_end");
static const std::string_view OGG_CAMERA_SHUTTER{
    static_cast<const char*>(ogg_camera_shutter_start),
    static_cast<size_t>(ogg_camera_shutter_end - ogg_camera_shutter_start)};
