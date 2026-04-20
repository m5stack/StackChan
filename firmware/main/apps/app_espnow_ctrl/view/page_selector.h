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

class PageSelector {
public:
    PageSelector(std::string_view label, const std::vector<std::string> options)
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setPadding(0, 0, 0, 0);
        _panel->setBgColor(lv_color_hex(0xF6F6F6));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);

        _label = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _label->setText(label);
        _label->setTextFont(&lv_font_montserrat_24);
        _label->setTextColor(lv_color_hex(0x26206A));
        _label->align(LV_ALIGN_CENTER, 0, -80);

        _roller = std::make_unique<uitk::lvgl_cpp::Roller>(_panel->get());
        _roller->setSize(200, 150);
        _roller->setOptions(options);
        _roller->align(LV_ALIGN_CENTER, -45, 35);
        _roller->onValueChanged().connect([&](uint32_t index) { _selected_index = index; });
        _roller->setTextFont(&lv_font_montserrat_24);
        _roller->setTextColor(lv_color_hex(0x26206A));
        _roller->setBgColor(lv_color_hex(0xDDEAFF));
        _roller->setRadius(18);
        _roller->setShadowWidth(0);
        _roller->setBorderWidth(0);

        _btn_confirm = std::make_unique<uitk::lvgl_cpp::Button>(_panel->get());
        _btn_confirm->label().setText("ok");
        _btn_confirm->label().setTextFont(&lv_font_montserrat_24);
        _btn_confirm->setSize(70, 110);
        _btn_confirm->align(LV_ALIGN_CENTER, 110, 40);
        _btn_confirm->onClick().connect([&]() { _is_selected = true; });
        _btn_confirm->setRadius(18);
        _btn_confirm->setShadowWidth(0);
    }

    bool update()
    {
        return _is_selected;
    }

    int getSelectedIndex() const
    {
        return _selected_index;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _roller;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label;
    bool _is_selected   = false;
    int _selected_index = 0;
};

/**
 * @brief Create a page selector and wait object
 *
 * @param label
 * @param options
 * @return int Selected option index
 */
static inline int create_page_selector_and_wait(std::string_view label, const std::vector<std::string>& options)
{
    GetHAL().lvglLock();
    auto page_selector = std::make_unique<view::PageSelector>(label, options);
    GetHAL().lvglUnlock();

    int index = 0;
    while (1) {
        GetHAL().delay(50);

        LvglLockGuard lock;
        if (page_selector->update()) {
            index = page_selector->getSelectedIndex();
            break;
        }
    }

    {
        LvglLockGuard lock;
        page_selector.reset();
    }

    return index;
}

}  // namespace view
