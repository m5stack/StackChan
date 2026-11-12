/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstdint>
#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <smooth_lvgl.hpp>
#include <assets/assets.h>
#include <hal/hal.h>
#include <memory>
#include <lvgl.h>
#include "bottom_swipe.h"

using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

/**
 * @brief
 *
 */
class HomeGesture {
public:
    std::function<void(void)> onGesture;

    void init()
    {
        _bottom_swipe = {};
    }

    void update()
    {
        lv_indev_t* indev = GetHAL().lvTouchpad;
        if (!indev) {
            return;
        }

        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);
        const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PR;
        if (_bottom_swipe.update(pressed, curr_point.x, curr_point.y) && onGesture) onGesture();
    }

private:
    view::BottomSwipe _bottom_swipe;
};

/**
 * @brief
 *
 */
class HomeButton {
public:
    HomeButton(lv_obj_t* parent, uint32_t colorButton, uint32_t colorBorder)
    {
        _bg_mask = std::make_unique<Container>(parent);
        _bg_mask->align(LV_ALIGN_CENTER, 0, 0);
        _bg_mask->setSize(320, 240);
        _bg_mask->setRadius(0);
        _bg_mask->setBgOpa(0);
        _bg_mask->setBorderWidth(0);
        // _bg_mask->setBgColor(lv_color_white());
        _bg_mask->addFlag(LV_OBJ_FLAG_CLICKABLE);
        _bg_mask->addFlag(LV_OBJ_FLAG_FLOATING);
        _bg_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _bg_mask->setPadding(0, 0, 0, 0);
        _bg_mask->onClick().connect([&]() { hide(); });

        _btn = std::make_unique<Button>(_bg_mask->get());
        _btn->setSize(152, 73);
        _btn->setAlign(LV_ALIGN_BOTTOM_MID);
        _btn->setBgColor(lv_color_hex(colorButton));
        _btn->setBorderWidth(2);
        _btn->setBorderColor(lv_color_hex(colorBorder));
        _btn->setShadowWidth(0);
        _btn->setRadius(18);
        _btn->addFlag(LV_OBJ_FLAG_FLOATING);
        _btn->onClick().connect([&]() { _is_clicked = true; });

        _icon = std::make_unique<Image>(_btn->get());
        _icon->align(LV_ALIGN_CENTER, 0, -10);
        _icon_home = assets::get_image("icon_home.bin");
        _icon->setSrc(&_icon_home);
        _icon->setImageRecolorOpa(LV_OPA_COVER);
        _icon->setImageRecolor(lv_color_hex(colorBorder));

        _btn->setPos(0, _pos_y_hide);
        _pos_y_anim.springOptions().bounce         = 0.1;
        _pos_y_anim.springOptions().visualDuration = 0.3;
        // _pos_y_anim.easingOptions().duration = 0.3;
        _pos_y_anim.teleport(_pos_y_hide);
    }

    void update()
    {
        _pos_y_anim.update();
        if (!_pos_y_anim.done()) {
            _btn->setPos(0, _pos_y_anim.directValue());
        } else {
            if (_hide_mask_flag) {
                _hide_mask_flag = false;
                _bg_mask->setHidden(true);
            }
        }
    }

    void show()
    {
        _hide_mask_flag = false;
        _bg_mask->moveForeground();
        _bg_mask->setHidden(false);
        _pos_y_anim = _pos_y_show;
        _is_hidden  = false;
    }

    void hide()
    {
        _pos_y_anim     = _pos_y_hide;
        _is_hidden      = true;
        _hide_mask_flag = true;
    }

    bool isHidden() const
    {
        return _is_hidden;
    }

    bool isClicked()
    {
        if (_is_clicked) {
            _is_clicked = false;
            return true;
        }
        return false;
    }

private:
    const int _pos_y_show = 22;
    const int _pos_y_hide = 75;

    std::unique_ptr<Container> _bg_mask;
    std::unique_ptr<Button> _btn;
    std::unique_ptr<Image> _icon;
    AnimateValue _pos_y_anim;
    bool _is_hidden      = false;
    bool _is_clicked     = false;
    bool _hide_mask_flag = false;
    lv_image_dsc_t _icon_home;
};

/**
 * @brief
 *
 */
class HomeIndicator {
public:
    std::function<void(void)> onGoHome;

    void init(lv_obj_t* parent, uint32_t colorButton, uint32_t colorBorder)
    {
        _home_gesture            = std::make_unique<HomeGesture>();
        _home_gesture->onGesture = [&]() { handle_home_gesture(); };
        _home_gesture->init();

        _home_button = std::make_unique<HomeButton>(parent, colorButton, colorBorder);
        _home_button->show();
        _button_show_tick = GetHAL().millis();
        _is_first_show    = true;
    }

    void update()
    {
        _home_gesture->update();
        _home_button->update();
        update_home_button_visibility();
        check_go_home();
    }

private:
    std::unique_ptr<HomeGesture> _home_gesture;
    std::unique_ptr<HomeButton> _home_button;
    uint32_t _button_show_tick = 0;
    bool _is_first_show        = true;

    void handle_home_gesture()
    {
        _home_button->show();
        _button_show_tick = GetHAL().millis();
    }

    void update_home_button_visibility()
    {
        if (!_home_button->isHidden()) {
            if (GetHAL().millis() - _button_show_tick > (_is_first_show ? 1600 : 3000)) {
                _is_first_show = false;
                _home_button->hide();
            }
        }
    }

    void check_go_home()
    {
        if (_home_button->isClicked()) {
            if (onGoHome) {
                onGoHome();
            }
        }
    }
};

/**
 * @brief
 *
 */
namespace view {

static std::unique_ptr<HomeIndicator> _home_indicator;

void create_home_indicator(std::function<void(void)> onGoHome, uint32_t colorButton, uint32_t colorBorder,
                           lv_obj_t* parent)
{
    _home_indicator           = std::make_unique<HomeIndicator>();
    _home_indicator->onGoHome = onGoHome;
    _home_indicator->init(parent, colorButton, colorBorder);
}

void update_home_indicator()
{
    if (_home_indicator) {
        _home_indicator->update();
    }
}

bool is_home_indicator_created()
{
    return _home_indicator != nullptr;
}

void destroy_home_indicator()
{
    _home_indicator.reset();
}

}  // namespace view
