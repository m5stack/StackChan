/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <lvgl.h>
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <stackchan/stackchan.h>
#include <assets/assets.h>
#include <string_view>
#include <memory>

namespace view {

class ReminderView : public stackchan::avatar::Decorator {
public:
    ReminderView(lv_obj_t* parent, std::string_view message)
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0x000000));
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);

        _msg_panel = std::make_unique<uitk::lvgl_cpp::Container>(_panel->get());
        _msg_panel->setBgColor(lv_color_hex(0xFFDF9A));
        _msg_panel->align(LV_ALIGN_CENTER, 0, -30);
        _msg_panel->setBorderWidth(0);
        _msg_panel->setSize(296, 156);
        _msg_panel->setRadius(18);

        _bell      = std::make_unique<uitk::lvgl_cpp::Image>(_msg_panel->get());
        _icon_bell = assets::get_image("icon_bell.bin");
        _bell->setSrc(&_icon_bell);
        _bell->align(LV_ALIGN_CENTER, -122, -59);
        _bell->setRotation(150);

        _title = std::make_unique<uitk::lvgl_cpp::Label>(_msg_panel->get());
        _title->align(LV_ALIGN_CENTER, -59, -59);
        _title->setText("Reminder:");
        _title->setTextFont(&lv_font_montserrat_16);
        _title->setTextColor(lv_color_hex(0x897039));

        _msg = std::make_unique<uitk::lvgl_cpp::Label>(_msg_panel->get());
        _msg->align(LV_ALIGN_CENTER, 0, 7);
        _msg->setText(message);
        _msg->setWidth(256);
        _msg->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _msg->setTextFont(&lv_font_montserrat_24);
        _msg->setTextColor(lv_color_hex(0x47330A));

        _btn_ok = std::make_unique<uitk::lvgl_cpp::Button>(_panel->get());
        _btn_ok->align(LV_ALIGN_CENTER, 0, 85);
        _btn_ok->setBgColor(lv_color_hex(0xB8D3FD));
        _btn_ok->setBorderWidth(0);
        _btn_ok->setShadowWidth(0);
        _btn_ok->setRadius(18);
        _btn_ok->setSize(296, 48);
        _btn_ok->label().setTextFont(&lv_font_montserrat_20);
        _btn_ok->label().setTextColor(lv_color_hex(0x26206A));
        _btn_ok->label().setText("OK");
        _btn_ok->onClick().connect([this]() { requestDestroy(); });
    }

    ~ReminderView()
    {
    }

    void _update() override
    {
        if (GetHAL().millis() - _anim_tick > 600) {
            _anim_tick = GetHAL().millis();
            _anim_flag = !_anim_flag;
            if (_anim_flag) {
                _bell->setRotation(150);
            } else {
                _bell->setRotation(-150);
            }
        }
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _msg_panel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _bell;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _msg;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_ok;
    lv_image_dsc_t _icon_bell;

    uint32_t _anim_tick = 0;
    bool _anim_flag     = false;
};

}  // namespace view
