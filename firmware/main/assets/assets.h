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
LV_IMG_DECLARE(icon_bell);
LV_IMG_DECLARE(icon_app_store);
LV_IMG_DECLARE(icon_ezdata);
LV_IMG_DECLARE(icon_wifi_high);
LV_IMG_DECLARE(icon_wifi_medium);
LV_IMG_DECLARE(icon_wifi_low);
LV_IMG_DECLARE(icon_wifi_slash);
LV_IMG_DECLARE(icon_bat_lightning);
LV_IMG_DECLARE(icon_dance);

extern const char ogg_camera_shutter_start[] asm("_binary_camera_shutter_ogg_start");
extern const char ogg_camera_shutter_end[] asm("_binary_camera_shutter_ogg_end");
static const std::string_view OGG_CAMERA_SHUTTER{
    static_cast<const char*>(ogg_camera_shutter_start),
    static_cast<size_t>(ogg_camera_shutter_end - ogg_camera_shutter_start)};

extern const char ogg_new_notification_start[] asm("_binary_new_notification_ogg_start");
extern const char ogg_new_notification_end[] asm("_binary_new_notification_ogg_end");
static const std::string_view OGG_NEW_NOTIFICATION{
    static_cast<const char*>(ogg_new_notification_start),
    static_cast<size_t>(ogg_new_notification_end - ogg_new_notification_start)};

namespace assets {

lv_image_dsc_t get_image(std::string_view name);

}  // namespace assets
