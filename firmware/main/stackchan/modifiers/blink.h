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
class BlinkModifier : public TimerModifier {
public:
    BlinkModifier(uint32_t destroyAfterMs = 0, uint32_t openIntervalMs = 5200, uint32_t closeIntervalMs = 200)
        : _open_interval_ms(openIntervalMs), _close_interval_ms(closeIntervalMs)
    {
        if (destroyAfterMs != 0) {
            scheduleDestroy(destroyAfterMs);
        }

        _should_close = true;
    }

    void resyncEyeWeights()
    {
        _needs_resync = true;
    }

    void _update(Modifiable& stackchan) override
    {
        TimerModifier::_update(stackchan);

        if (!stackchan.hasAvatar()) {
            return;
        }

        if (_needs_resync) {
            _needs_resync     = false;
            _left_eye_weight  = stackchan.avatar().leftEye().getWeight();
            _right_eye_weight = stackchan.avatar().rightEye().getWeight();
        }

        if (_should_close) {
            _should_close = false;

            _left_eye_weight  = stackchan.avatar().leftEye().getWeight();
            _right_eye_weight = stackchan.avatar().rightEye().getWeight();

            stackchan.avatar().leftEye().setWeight(25);
            stackchan.avatar().rightEye().setWeight(25);

            getTimer().addTask(_close_interval_ms, 1, 0, [this]() { _should_open = true; });
        }

        if (_should_open) {
            _should_open = false;

            stackchan.avatar().leftEye().setWeight(_left_eye_weight);
            stackchan.avatar().rightEye().setWeight(_right_eye_weight);

            getTimer().addTask(_open_interval_ms, 1, 0, [this]() { _should_close = true; });
        }
    }

private:
    uint32_t _open_interval_ms;
    uint32_t _close_interval_ms;

    bool _should_close    = false;
    bool _should_open     = false;
    bool _needs_resync    = false;
    int _left_eye_weight  = 0;
    int _right_eye_weight = 0;
};

}  // namespace stackchan
