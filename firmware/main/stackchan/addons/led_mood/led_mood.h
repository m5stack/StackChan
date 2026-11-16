/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../../modifiable.h"
#include <hal/hal.h>
#include <cmath>
#include <cstdint>

namespace stackchan::addon {

/**
 * @brief Drives both neon light rows from the robot's conversation state and current emotion.
 *
 * - Connecting: slow amber pulse
 * - Listening: steady soft green
 * - Speaking: pulse tinted by the face's emotion (neutral blue, happy gold, angry red, sad deep blue, doubt purple)
 * - Idle / Sleeping: off
 *
 * Priority: alert (e.g. a reefwatch detection) > manual color (set_led_color voice tool) > mood.
 * Colors are only written when they change, so other LED users (app avatar control, dances) still work while idle.
 */
class LedMood {
public:
    enum class State {
        Idle,
        Connecting,
        Listening,
        Speaking,
        Sleeping,
    };

    struct Rgb {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        bool operator==(const Rgb& o) const
        {
            return r == o.r && g == o.g && b == o.b;
        }
        bool operator!=(const Rgb& o) const
        {
            return !(*this == o);
        }
    };

    void setState(State state)
    {
        if (_state == state) {
            return;
        }
        _state       = state;
        _state_since = GetHAL().millis();
        _dirty       = true;
    }

    State getState() const
    {
        return _state;
    }

    void setOverride(uint8_t r, uint8_t g, uint8_t b)
    {
        _override     = {r, g, b};
        _has_override = true;
        _dirty        = true;
    }

    void clearOverride()
    {
        _has_override = false;
        _dirty        = true;
    }

    /**
     * @brief Temporary alert color that wins over everything until clearAlert()
     */
    void setAlert(uint8_t r, uint8_t g, uint8_t b)
    {
        _alert     = {r, g, b};
        _has_alert = true;
        _dirty     = true;
    }

    void clearAlert()
    {
        _has_alert = false;
        _dirty     = true;
    }

    bool hasOverride() const
    {
        return _has_override;
    }

    void setEnabled(bool enabled)
    {
        _enabled = enabled;
        _dirty   = true;
    }

    bool isEnabled() const
    {
        return _enabled;
    }

    void update(Modifiable& stackchan)
    {
        if (!_enabled) {
            return;
        }

        uint32_t now = GetHAL().millis();
        if (!_dirty && now - _last_tick < _tick_interval_ms) {
            return;
        }
        _last_tick = now;
        _dirty     = false;

        Rgb target = compute_target(stackchan, now);
        if (target == _last_written) {
            return;
        }
        _last_written = target;

        stackchan.leftNeonLight().setColor(target.r, target.g, target.b);
        stackchan.rightNeonLight().setColor(target.r, target.g, target.b);
    }

private:
    // Keep brightness well under the 168 "safe" maximum used elsewhere; these rows sit close to the user's eyes.
    static constexpr Rgb _listening_color  = {0, 60, 10};
    static constexpr Rgb _connecting_color = {70, 35, 0};
    static constexpr uint32_t _tick_interval_ms = 60;

    State _state           = State::Idle;
    uint32_t _state_since  = 0;
    uint32_t _last_tick    = 0;
    bool _dirty            = true;
    bool _enabled          = true;
    bool _has_override     = false;
    bool _has_alert        = false;
    Rgb _alert;
    Rgb _override;
    Rgb _last_written;  // Starts as off, so an idle robot never touches LEDs other code has set

    static Rgb emotion_color(Modifiable& stackchan)
    {
        if (!stackchan.hasAvatar()) {
            return {0, 20, 80};
        }
        switch (stackchan.avatar().getEmotion()) {
            case avatar::Emotion::Happy:
                return {90, 60, 0};
            case avatar::Emotion::Angry:
                return {100, 0, 0};
            case avatar::Emotion::Sad:
                return {0, 0, 70};
            case avatar::Emotion::Doubt:
                return {60, 0, 80};
            case avatar::Emotion::Sleepy:
                return {10, 5, 20};
            default:
                return {0, 25, 90};
        }
    }

    // 0.25 .. 1.0 sine pulse
    static float pulse(uint32_t elapsedMs, uint32_t cycleMs)
    {
        float phase = static_cast<float>(elapsedMs % cycleMs) / static_cast<float>(cycleMs);
        return 0.625f + 0.375f * sinf(phase * 2.0f * static_cast<float>(M_PI));
    }

    static Rgb scale(const Rgb& c, float k)
    {
        return {static_cast<uint8_t>(c.r * k), static_cast<uint8_t>(c.g * k), static_cast<uint8_t>(c.b * k)};
    }

    Rgb compute_target(Modifiable& stackchan, uint32_t now) const
    {
        if (_has_alert) {
            return _alert;
        }
        if (_has_override) {
            return _override;
        }

        uint32_t elapsed = now - _state_since;
        switch (_state) {
            case State::Connecting:
                return scale(_connecting_color, pulse(elapsed, 2000));
            case State::Listening:
                return _listening_color;
            case State::Speaking:
                return scale(emotion_color(stackchan), pulse(elapsed, 1200));
            case State::Sleeping:
            case State::Idle:
            default:
                return {0, 0, 0};
        }
    }
};

}  // namespace stackchan::addon
