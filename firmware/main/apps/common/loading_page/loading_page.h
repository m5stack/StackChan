/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <cstdint>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

class LoadingPage {
public:
    LoadingPage(uint32_t bgColor = 0x000000, uint32_t textColor = 0xFFFFFF)
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(bgColor));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(320, 240);
        _panel->setBorderWidth(0);
        _panel->setRadius(0);

        _msg = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _msg->setTextFont(&lv_font_montserrat_20);
        _msg->setTextColor(lv_color_hex(textColor));
        _msg->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _msg->align(LV_ALIGN_CENTER, 0, 0);
        _msg->setText("");
        _msg->setWidth(220);
    }

    void setMessage(std::string_view msg)
    {
        _msg->setText(msg);
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _msg;
};

}  // namespace view
