/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ws_call.h"
#include <smooth_ui_toolkit.hpp>
#include <mooncake_log.h>
#include <functional>
#include <cstdint>
#include <memory>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace smooth_ui_toolkit;
using namespace view;

LV_IMAGE_DECLARE(icon_phone);

/**
 * @brief
 *
 */
class IncomingPanel : public Container {
public:
    std::function<void(void)> onAccept;
    std::function<void(void)> onDecline;

    IncomingPanel(lv_obj_t* parent, std::string caller) : Container(parent)
    {
        setSize(320, 240);
        align(LV_ALIGN_CENTER, 0, 0);
        setBorderWidth(0);
        setBgOpa(0);

        _label_caller = std::make_unique<Label>(get());
        _label_caller->setTextFont(&lv_font_montserrat_24);
        _label_caller->setTextColor(lv_color_hex(0xFFFFFF));
        _label_caller->align(LV_ALIGN_CENTER, 0, -63);
        _label_caller->setText(caller);

        _button_decline = std::make_unique<Button>(get());
        _button_decline->align(LV_ALIGN_CENTER, -73, 30);
        _button_decline->setBgColor(lv_color_hex(0xEB5545));
        _button_decline->setRadius(LV_RADIUS_CIRCLE);
        _button_decline->setShadowWidth(0);
        _button_decline->setBorderWidth(0);
        _button_decline->setSize(64, 64);
        _button_decline->onClick().connect([&]() {
            if (onDecline) {
                onDecline();
            }
        });

        _img_decline = std::make_unique<Image>(_button_decline->get());
        _img_decline->align(LV_ALIGN_CENTER, 0, 0);
        _img_decline->setRotation(1325);
        _img_decline->setSrc(&icon_phone);

        _label_decline = std::make_unique<Label>(get());
        _label_decline->setTextFont(&lv_font_montserrat_14);
        _label_decline->setTextColor(lv_color_hex(0xFFFFFF));
        _label_decline->align(LV_ALIGN_CENTER, -73, 77);
        _label_decline->setText("Decline");

        _button_accept = std::make_unique<Button>(get());
        _button_accept->align(LV_ALIGN_CENTER, 73, 30);
        _button_accept->setBgColor(lv_color_hex(0x67CE67));
        _button_accept->setRadius(LV_RADIUS_CIRCLE);
        _button_accept->setShadowWidth(0);
        _button_accept->setBorderWidth(0);
        _button_accept->setSize(64, 64);
        _button_accept->onClick().connect([&]() {
            if (onAccept) {
                onAccept();
            }
        });

        _img_accept = std::make_unique<Image>(_button_accept->get());
        _img_accept->align(LV_ALIGN_CENTER, 0, 0);
        _img_accept->setSrc(&icon_phone);

        _label_accept = std::make_unique<Label>(get());
        _label_accept->setTextFont(&lv_font_montserrat_14);
        _label_accept->setTextColor(lv_color_hex(0xFFFFFF));
        _label_accept->align(LV_ALIGN_CENTER, 73, 77);
        _label_accept->setText("Accept");
    }

    void update()
    {
        if (GetHAL().millis() - _time_count > 600) {
            _anim_flag = !_anim_flag;
            if (_anim_flag) {
                _img_accept->setRotation(-200);
            } else {
                _img_accept->setRotation(0);
            }

            _time_count = GetHAL().millis();
        }
    }

private:
    std::unique_ptr<Label> _label_caller;
    std::unique_ptr<Label> _label_decline;
    std::unique_ptr<Label> _label_accept;
    std::unique_ptr<Button> _button_decline;
    std::unique_ptr<Button> _button_accept;
    std::unique_ptr<Image> _img_decline;
    std::unique_ptr<Image> _img_accept;

    uint32_t _time_count = 0;
    bool _anim_flag      = false;
};

/**
 * @brief
 *
 */
class ConnectingPanel : public Container {
public:
    std::function<void(void)> onEnd;

    ConnectingPanel(lv_obj_t* parent, std::string caller) : Container(parent)
    {
        setSize(320, 240);
        align(LV_ALIGN_CENTER, 0, 0);
        setPadding(0, 0, 0, 0);
        setBorderWidth(0);
        setBgOpa(0);

        _label_caller = std::make_unique<Label>(get());
        _label_caller->setTextFont(&lv_font_montserrat_16);
        _label_caller->setTextColor(lv_color_hex(0xFFFFFF));
        _label_caller->align(LV_ALIGN_LEFT_MID, 13, 85);
        _label_caller->setText(caller);

        _label_timer = std::make_unique<Label>(get());
        _label_timer->setTextFont(&lv_font_montserrat_14);
        _label_timer->setTextColor(lv_color_hex(0x67CE67));
        _label_timer->align(LV_ALIGN_LEFT_MID, 13, 104);

        _button_end = std::make_unique<Button>(get());
        _button_end->align(LV_ALIGN_CENTER, 116, 94);
        _button_end->setBgColor(lv_color_hex(0xEB5545));
        _button_end->setRadius(10);
        _button_end->setShadowWidth(0);
        _button_end->setBorderWidth(0);
        _button_end->setSize(72, 36);
        _button_end->onClick().connect([&]() {
            if (onEnd) {
                onEnd();
            }
        });
        _button_end->label().setTextFont(&lv_font_montserrat_14);
        _button_end->label().setTextColor(lv_color_hex(0xFFFFFF));
        _button_end->label().setText("End");
    }

    void reset()
    {
        _connect_time_count = GetHAL().millis();
        update_timer_label();
    }

    void update_timer_label()
    {
        uint32_t elapsed = (GetHAL().millis() - _connect_time_count) / 1000;
        uint32_t min     = elapsed / 60;
        uint32_t sec     = elapsed % 60;

        _label_timer->setText(fmt::format("{:02}:{:02}", min, sec));
    }

    void update()
    {
        if (GetHAL().millis() - _time_count > 1000) {
            update_timer_label();
            _time_count = GetHAL().millis();
        }
    }

private:
    std::unique_ptr<Label> _label_caller;
    std::unique_ptr<Label> _label_timer;
    std::unique_ptr<Button> _button_end;

    uint32_t _time_count         = 0;
    uint32_t _connect_time_count = 0;
};

WsCallView::WsCallView(lv_obj_t* parent, std::string caller)
{
    auto panel_incoming       = std::make_unique<IncomingPanel>(parent, caller);
    panel_incoming->onAccept  = [&]() { handle_accept(); };
    panel_incoming->onDecline = [&]() { handle_decline(); };

    auto panel_connecting   = std::make_unique<ConnectingPanel>(parent, caller);
    panel_connecting->onEnd = [&]() { handle_end(); };
    panel_connecting->setHidden(true);

    _panel_incoming   = std::move(panel_incoming);
    _panel_connecting = std::move(panel_connecting);

    _state = State::Incoming;
}

void WsCallView::_update()
{
    switch (_state) {
        case State::Incoming: {
            auto icoming_panel = static_cast<IncomingPanel*>(_panel_incoming.get());
            icoming_panel->update();
            break;
        }
        case State::Connecting: {
            auto connecting_panel = static_cast<ConnectingPanel*>(_panel_connecting.get());
            if (_panel_incoming) {
                _panel_incoming.reset();
                connecting_panel->setHidden(false);
                connecting_panel->reset();
            }
            connecting_panel->update();
            break;
        }
        default:
            break;
    }
}

void WsCallView::handle_accept()
{
    _state = State::Connecting;
    if (onAccept) {
        onAccept();
    }
}

void WsCallView::handle_decline()
{
    if (onDecline) {
        onDecline();
    }
    if (onDestory) {
        onDestory();
    }
    requestDestroy();
}

void WsCallView::handle_end()
{
    if (onEnd) {
        onEnd();
    }
    if (onDestory) {
        onDestory();
    }
    requestDestroy();
}
