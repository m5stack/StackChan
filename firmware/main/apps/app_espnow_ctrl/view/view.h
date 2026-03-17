/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <memory>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <hal/hal.h>
#include <vector>
#include <string>
#include <string_view>

namespace view {

class EspnowRoleSelectorPage {
public:
    EspnowRoleSelectorPage()
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setSize(320, 240);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setBgColor(lv_color_hex(0xE7FFE0));
        _panel->setPadding(0, 0, 0, 0);
        _panel->setPadRow(18);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);

        _title = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _title->setText("Select Role:");
        _title->setTextFont(&lv_font_montserrat_24);
        _title->setTextColor(lv_color_hex(0x154311));
        _title->align(LV_ALIGN_TOP_MID, 0, 12);

        _btn_receiver = std::make_unique<uitk::lvgl_cpp::Button>(*_panel);
        _btn_receiver->align(LV_ALIGN_TOP_MID, 0, 58);
        _btn_receiver->setSize(282, 48);
        _btn_receiver->setRadius(18);
        _btn_receiver->setBgColor(lv_color_hex(0xA0D99C));
        _btn_receiver->setBorderWidth(0);
        _btn_receiver->setShadowWidth(0);
        _btn_receiver->label().setText("Receiver");
        _btn_receiver->label().setTextFont(&lv_font_montserrat_24);
        _btn_receiver->label().setTextColor(lv_color_hex(0x154311));
        _btn_receiver->onClick().connect([this]() { _selected_index = 0; });

        _btn_sender = std::make_unique<uitk::lvgl_cpp::Button>(*_panel);
        _btn_sender->align(LV_ALIGN_TOP_MID, 0, 126);
        _btn_sender->setSize(282, 48);
        _btn_sender->setRadius(18);
        _btn_sender->setBgColor(lv_color_hex(0xA0D99C));
        _btn_sender->setBorderWidth(0);
        _btn_sender->setShadowWidth(0);
        _btn_sender->label().setText("Sender");
        _btn_sender->label().setTextFont(&lv_font_montserrat_24);
        _btn_sender->label().setTextColor(lv_color_hex(0x154311));
        _btn_sender->onClick().connect([this]() { _selected_index = 1; });

        _btn_advanced = std::make_unique<uitk::lvgl_cpp::Button>(*_panel);
        _btn_advanced->align(LV_ALIGN_TOP_MID, 0, 194);
        _btn_advanced->setSize(170, 30);
        _btn_advanced->setRadius(18);
        _btn_advanced->setBgColor(lv_color_hex(0xB9E6B4));
        _btn_advanced->setBorderWidth(0);
        _btn_advanced->setShadowWidth(0);
        _btn_advanced->label().setText("Advanced");
        _btn_advanced->label().setTextFont(&lv_font_montserrat_16);
        _btn_advanced->label().setTextColor(lv_color_hex(0x5F8559));
        _btn_advanced->onClick().connect([this]() { _selected_index = 2; });
    }

    bool isSelected()
    {
        return _selected_index != -1;
    }

    int selectedIndex()
    {
        return _selected_index;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_receiver;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_sender;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_advanced;
    int _selected_index = -1;
};

}  // namespace view
