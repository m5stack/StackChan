/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <stackchan/stackchan.h>
#include <memory>

namespace view {

/**
 * @brief
 *
 */
class WsCallView : public stackchan::avatar::Decorator {
public:
    std::function<void(void)> onAccept;
    std::function<void(void)> onDecline;
    std::function<void(void)> onEnd;
    std::function<void(void)> onDestory;

    enum class State {
        Incoming = 0,
        Connecting,
    };

    WsCallView(lv_obj_t* parent, std::string caller);

private:
    State _state = State::Incoming;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_incoming;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_connecting;

    void _update() override;

    void handle_accept();
    void handle_decline();
    void handle_end();
};

}  // namespace view
