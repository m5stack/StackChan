/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "timed.h"
#include "../utils/random.h"
#include <smooth_ui_toolkit.hpp>
#include <cstdint>

namespace stackchan {

class IdleMotionModifier : public TimerModifier {
public:
    IdleMotionModifier(uint32_t interval_min = 2000, uint32_t interval_max = 8000)
        : _interval_min(interval_min), _interval_max(interval_max)
    {
        schedule_next_move();
    }

    void pause()
    {
        _paused = true;
    }

    void resume()
    {
        if (_paused) {
            _paused = false;
            if (!_timer_active) {
                schedule_next_move();
            }
        }
    }

    void _update(Modifiable& stackchan) override
    {
        TimerModifier::_update(stackchan);

        if (_trigger_move) {
            _trigger_move = false;
            _timer_active = false;
            if (!_paused) {
                perform_idle_motion(stackchan);
                schedule_next_move();
            }
        }
    }

private:
    void schedule_next_move()
    {
        if (_paused) {
            _timer_active = false;
            return;
        }
        _timer_active  = true;
        uint32_t delay = Random::getInstance().getInt(_interval_min, _interval_max);
        getTimer().addTask(delay, 1, 0, [this]() { _trigger_move = true; });
    }

    void perform_idle_motion(Modifiable& stackchan)
    {
        auto& motion = stackchan.motion();
        int action   = Random::getInstance().getInt(0, 10);  // Probabilistic choice

        int32_t current_yaw   = motion.getCurrentYawAngle();
        int32_t current_pitch = motion.getCurrentPitchAngle();

        int32_t target_yaw   = current_yaw;
        int32_t target_pitch = current_pitch;
        int speed            = 400;

        if (action < 5) {
            // 50% chance: Casual look around (Look at a random point in front)
            target_yaw   = Random::getInstance().getInt(-300, 300);
            target_pitch = Random::getInstance().getInt(100, 250);
            speed        = Random::getInstance().getInt(200, 500);  // Slow to medium speed
        } else if (action < 7) {
            // 20% chance: Small adjustment / "Breathing" (Very small movement from current)
            target_yaw += Random::getInstance().getInt(-100, 100);
            target_pitch += Random::getInstance().getInt(-50, 50);
            speed = Random::getInstance().getInt(100, 300);  // Very slow
        } else if (action < 9) {
            // 20% chance: Quick glance (Faster, smaller range)
            target_yaw += Random::getInstance().getInt(-100, 100);
            target_pitch += Random::getInstance().getInt(-30, 30);
            speed = Random::getInstance().getInt(300, 600);  // Fast
        } else {
            // 10% chance: Return to center / Neutral
            target_yaw   = 0;
            target_pitch = 200;  // Slightly looking up/forward
            speed        = 100;
        }

        target_yaw   = uitk::clamp(target_yaw, -1280, 1280);
        target_pitch = uitk::clamp(target_pitch, 0, 900);
        speed        = uitk::clamp(speed, 0, 1000);

        motion.moveWithSpeed(target_yaw, target_pitch, speed);
    }

    uint32_t _interval_min;
    uint32_t _interval_max;
    bool _trigger_move = false;
    bool _paused       = false;
    bool _timer_active = false;
};

}  // namespace stackchan
