/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// Ref: https://www.heroui.com/docs/components/toast
#include <cstdint>
#include <string>
#include <lvgl.h>
#include <mooncake.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <string_view>
#include <memory>
#include <vector>

namespace view {

enum class ToastType {
    Info = 0,
    Warning,
    Error,
    Success,
    Orange,
    Gray,
    Rose,
    Dark,
};

class Toast {
public:
    enum class State {
        Closed,
        Opening,
        Opened,
        Closing,
    };

    struct KeyFrame_t {
        int16_t y = 0;
        int16_t w = 0;
    };

    struct Config_t {
        ToastType type      = ToastType::Info;
        uint32_t durationMs = 1000;
        std::string msg;
    };

    Config_t config;

    void init(lv_obj_t* parent);
    void update();
    void close(bool teleport = false);
    void open(bool teleport = false);
    void stack(bool teleport = false);
    inline uitk::lvgl_cpp::Container* get()
    {
        return _toast.get();
    }
    inline State getState() const
    {
        return _state;
    }

protected:
    uint32_t _time_count = 0;
    State _state         = State::Closed;
    uint8_t _stack_depth = 0;

    std::unique_ptr<uitk::lvgl_cpp::Container> _toast;
    std::unique_ptr<uitk::lvgl_cpp::Label> _msg_label;

    uitk::AnimateValue _anim_y;
    uitk::AnimateValue _anim_w;

    void update_anim(const KeyFrame_t& target, bool teleport);
};

class ToastManager : public mooncake::BasicAbility {
public:
    void onCreate() override;
    void onRunning() override;

protected:
    int _current_toast_index = 0;
    std::vector<std::unique_ptr<Toast>> _toast_list;
};

void pop_a_toast(std::string_view msg, ToastType type = ToastType::Info, uint32_t durationMs = 1600);

}  // namespace view
