/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <string_view>
#include <vector>
#include <string>
#include <memory>

namespace view {

/**
 * @brief
 *
 */
class EzdataGuidePage {
public:
    EzdataGuidePage(std::string_view pairCode)
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0x60A5FA));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);
        _panel->setPadding(0, 0, 0, 0);

        _panel_url = std::make_unique<uitk::lvgl_cpp::Container>(*_panel);
        _panel_url->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel_url->setBgColor(lv_color_hex(0xFFFFFF));
        _panel_url->align(LV_ALIGN_TOP_MID, 0, 42);
        _panel_url->setBorderWidth(0);
        _panel_url->setSize(296, 51);
        _panel_url->setRadius(18);

        _panel_pair_code = std::make_unique<uitk::lvgl_cpp::Container>(*_panel);
        _panel_pair_code->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel_pair_code->setBgColor(lv_color_hex(0xFFFFFF));
        _panel_pair_code->align(LV_ALIGN_TOP_MID, 0, 138);
        _panel_pair_code->setBorderWidth(0);
        _panel_pair_code->setSize(296, 86);
        _panel_pair_code->setRadius(18);

        _title_url = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _title_url->setText("Ezdata Web client:");
        _title_url->setTextFont(&lv_font_montserrat_16);
        _title_url->setTextColor(lv_color_hex(0x0E2648));
        _title_url->align(LV_ALIGN_TOP_LEFT, 27, 16);

        _msg_url = std::make_unique<uitk::lvgl_cpp::Label>(*_panel_url);
        _msg_url->setText("https://my.m5stack.com/ezdata2");
        _msg_url->setTextFont(&lv_font_montserrat_16);
        _msg_url->setTextColor(lv_color_hex(0x0E2648));
        _msg_url->align(LV_ALIGN_CENTER, 0, 0);

        _title_pair_code = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _title_pair_code->setText("Pair Code:");
        _title_pair_code->setTextFont(&lv_font_montserrat_16);
        _title_pair_code->setTextColor(lv_color_hex(0x0E2648));
        _title_pair_code->align(LV_ALIGN_TOP_LEFT, 27, 113);

        _msg_pair_code = std::make_unique<uitk::lvgl_cpp::Label>(*_panel_pair_code);
        _msg_pair_code->setText(pairCode);
        _msg_pair_code->setTextFont(&lv_font_montserrat_24);
        _msg_pair_code->setTextColor(lv_color_hex(0x0E2648));
        _msg_pair_code->align(LV_ALIGN_CENTER, 0, 0);
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_url;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_pair_code;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title_url;
    std::unique_ptr<uitk::lvgl_cpp::Label> _msg_url;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title_pair_code;
    std::unique_ptr<uitk::lvgl_cpp::Label> _msg_pair_code;
};

}  // namespace view
