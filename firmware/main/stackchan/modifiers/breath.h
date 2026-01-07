/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "timed.h"
#include <cstdint>

namespace stackchan {

/**
 * @brief
 *
 */
class BreathModifier : public TimerModifier {
public:
    BreathModifier(uint32_t destroyAfterMs = 0, uint32_t updateIntervalMs = 600)
    {
        if (destroyAfterMs != 0) {
            scheduleDestroy(destroyAfterMs);
        }

        if (updateIntervalMs == 0) {
            requestDestroy();
        }

        getTimer().addTask(updateIntervalMs, -1, 0, [this]() { _update_flag = true; });
    }

    void _update(Modifiable& stackchan) override
    {
        TimerModifier::_update(stackchan);

        if (!stackchan.hasAvatar()) {
            return;
        }

        if (!_update_flag) {
            return;
        }
        _update_flag = false;

        // Check state switch
        _breathe_count++;
        if (_is_breathing_in) {
            if (_breathe_count >= _breathe_in_count) {
                _is_breathing_in = false;
                _breathe_count   = 0;
            }
        } else {
            if (_breathe_count >= _breathe_out_count) {
                _is_breathing_in = true;
                _breathe_count   = 0;
            }
        }

        // Update breath
        auto& avatar        = stackchan.avatar();
        _left_eye_position  = avatar.leftEye().getPosition();
        _right_eye_position = avatar.rightEye().getPosition();
        _mouth_position     = avatar.mouth().getPosition();

        if (_is_breathing_in) {
            _left_eye_position.y += _breathe_in_offset;
            _right_eye_position.y += _breathe_in_offset;
            _mouth_position.y += _breathe_in_offset;
        } else {
            _left_eye_position.y += _breathe_out_offset;
            _right_eye_position.y += _breathe_out_offset;
            _mouth_position.y += _breathe_out_offset;
        }

        avatar.leftEye().setPosition(_left_eye_position);
        avatar.rightEye().setPosition(_right_eye_position);
        avatar.mouth().setPosition(_mouth_position);
    }

private:
    // Keep in and out total offset added up to 0
    const int _breathe_in_count   = 7;
    const int _breathe_in_offset  = -4;
    const int _breathe_out_count  = 7;
    const int _breathe_out_offset = 4;

    bool _update_flag     = false;
    bool _is_breathing_in = true;
    int _breathe_count    = 0;

    uitk::Vector2i _left_eye_position;
    uitk::Vector2i _right_eye_position;
    uitk::Vector2i _mouth_position;
};

}  // namespace stackchan
