/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <functional>

namespace view {

void create_home_indicator(std::function<void(void)> onGoHome, lv_obj_t* parent = lv_screen_active());
void update_home_indicator();
bool is_home_indicator_created();
void destroy_home_indicator();

}  // namespace view
