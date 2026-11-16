/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <hal/hal.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace stackchan {

/**
 * @brief Short head gesture (nod / shake) built from servo offsets around the current pose.
 * Moves only the servos, so the face keeps its expression. Returns to the starting pose when done.
 */
class HeadGestureModifier : public Modifier {
public:
    struct Step {
        int yawOffset;    // degrees * 10, relative to the start pose
        int pitchOffset;  // degrees * 10, relative to the start pose
        uint32_t durationMs;
    };

    static std::vector<Step> Nod(int times = 2)
    {
        std::vector<Step> steps;
        for (int i = 0; i < times; i++) {
            steps.push_back({0, -150, 280});
            steps.push_back({0, 0, 280});
        }
        return steps;
    }

    static std::vector<Step> Shake(int times = 2)
    {
        std::vector<Step> steps;
        for (int i = 0; i < times; i++) {
            steps.push_back({-220, 0, 300});
            steps.push_back({220, 0, 300});
        }
        steps.push_back({0, 0, 300});
        return steps;
    }

    HeadGestureModifier(std::vector<Step> steps, int speed = 700) : _steps(std::move(steps)), _speed(speed)
    {
    }

    void _update(Modifiable& stackchan) override
    {
        auto& motion = stackchan.motion();
        uint32_t now = GetHAL().millis();

        if (!_started) {
            _started    = true;
            _base_yaw   = motion.yawServo().getCurrentAngle();
            _base_pitch = motion.pitchServo().getCurrentAngle();
            // Keep room to nod down: pitch range is 0..900 (0 = level, 900 = up)
            _base_pitch = std::max(_base_pitch, 200);
            _step_index = -1;
            _step_end   = now;
        }

        if (now < _step_end) {
            return;
        }

        _step_index++;
        if (_step_index >= static_cast<int>(_steps.size())) {
            motion.yawServo().moveWithSpeed(_base_yaw, _speed);
            motion.pitchServo().moveWithSpeed(_base_pitch, _speed);
            requestDestroy();
            return;
        }

        const auto& step = _steps[_step_index];
        motion.yawServo().moveWithSpeed(std::clamp(_base_yaw + step.yawOffset, -1280, 1280), _speed);
        motion.pitchServo().moveWithSpeed(std::clamp(_base_pitch + step.pitchOffset, 0, 900), _speed);
        _step_end = now + step.durationMs;
    }

private:
    std::vector<Step> _steps;
    int _speed;
    bool _started     = false;
    int _base_yaw     = 0;
    int _base_pitch   = 0;
    int _step_index   = -1;
    uint32_t _step_end = 0;
};

}  // namespace stackchan
