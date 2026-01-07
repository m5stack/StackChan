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

/**
 * @brief
 *
 */
class SpeakingModifier : public TimerModifier {
public:
    SpeakingModifier(uint32_t destroyAfterMs = 0, uint32_t updateIntervalMs = 180, bool enableMotion = true)
        : _enable_motion(enableMotion)
    {
        if (destroyAfterMs != 0) {
            getTimer().addTask(destroyAfterMs, -1, 0, [this]() { _destroy_flag = true; });
        }

        if (updateIntervalMs == 0) {
            requestDestroy();
        }

        getTimer().addTask(updateIntervalMs, -1, 0, [this]() { _update_flag = true; });

        if (_enable_motion) {
            schedule_next_motion();
        }
    }

    void _update(Modifiable& stackchan) override
    {
        TimerModifier::_update(stackchan);

        if (!stackchan.hasAvatar()) {
            return;
        }

        if (_motion_flag) {
            _motion_flag = false;
            perform_speaking_motion(stackchan);
            schedule_next_motion();
        }

        if (!_update_flag) {
            return;
        }
        _update_flag = false;

        if (_destroy_flag) {
            stackchan.avatar().mouth().setWeight(0);
            requestDestroy();
            return;
        }

        auto& random = Random::getInstance();

        _is_opened = !_is_opened;
        if (_is_opened) {
            stackchan.avatar().mouth().setWeight(random.getInt(_open_min_weight, _open_max_weight));
        } else {
            stackchan.avatar().mouth().setWeight(random.getInt(_close_min_weight, _close_max_weight));
        }
    }

private:
    void schedule_next_motion()
    {
        uint32_t delay = Random::getInstance().getInt(1600, 2200);
        getTimer().addTask(delay, 1, 0, [this]() { _motion_flag = true; });
    }

    void perform_speaking_motion(Modifiable& stackchan)
    {
        auto& motion = stackchan.motion();
        int action   = Random::getInstance().getInt(0, 10);

        int32_t current_yaw   = motion.getCurrentYawAngle();
        int32_t current_pitch = motion.getCurrentPitchAngle();

        int32_t target_yaw   = current_yaw;
        int32_t target_pitch = current_pitch;
        int speed            = 120;

        if (action < 4) {
            // Emphasize: Nod / Head bob
            target_pitch += Random::getInstance().getInt(-30, 60);
            speed = Random::getInstance().getInt(100, 160);
        } else if (action < 7) {
            // Look at audience: Small yaw changes
            target_yaw += Random::getInstance().getInt(-60, 60);
            target_pitch += Random::getInstance().getInt(-30, 30);
            speed = Random::getInstance().getInt(160, 200);
        } else if (action < 9) {
            // Reset / Center attention
            target_yaw   = Random::getInstance().getInt(-60, 60);
            target_pitch = 200 + Random::getInstance().getInt(-50, 50);
            speed        = 120;
        }

        target_yaw   = uitk::clamp(target_yaw, -1280, 1280);
        target_pitch = uitk::clamp(target_pitch, 0, 900);

        motion.moveWithSpeed(target_yaw, target_pitch, speed);
    }

    const int _open_min_weight  = 40;
    const int _open_max_weight  = 70;
    const int _close_min_weight = 0;
    const int _close_max_weight = 30;

    bool _enable_motion = false;
    bool _motion_flag   = false;
    bool _update_flag   = false;
    bool _destroy_flag  = false;
    bool _is_opened     = false;
};

}  // namespace stackchan
