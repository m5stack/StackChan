/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "timed.h"
#
#include <cstdint>

namespace stackchan {

/**
 * @brief
 *
 */
class IMUMotionModifier : public TimerModifier {
public:
    IMUMotionModifier()
    {
        GetHAL().onImuMotionEvent.connect([&](ImuMotionEvent event) {
            if (_is_applied) {
                return;
            }
            _new_event = event;
        });
    }

    void _update(Modifiable& stackchan) override
    {
        TimerModifier::_update(stackchan);

        if (_new_event != ImuMotionEvent::None) {
            _new_event = ImuMotionEvent::None;
            apply(stackchan, false);
            _is_applied   = true;
            _restore_flag = false;
            getTimer().addTask(2000, 0, 0, [&]() { _restore_flag = true; });
        }

        if (_restore_flag) {
            apply(stackchan, true);
            _restore_flag = false;
            _is_applied   = false;
        }
    }

    void apply(Modifiable& stackchan, bool isRestore)
    {
        auto& avatar = stackchan.avatar();
        if (isRestore) {
            avatar.leftEye().setPosition({0, 0});
            avatar.rightEye().setPosition({0, 0});
            avatar.leftEye().setSize(0);
            avatar.rightEye().setSize(0);
            avatar.mouth().setPosition({0, 0});
            avatar.mouth().setWeight(0);
        } else {
            avatar.leftEye().setPosition({-20, 0});
            avatar.rightEye().setPosition({20, 0});
            avatar.leftEye().setSize(60);
            avatar.rightEye().setSize(60);
            avatar.mouth().setPosition({0, -40});
            avatar.mouth().setWeight(58);
        }
    }

private:
    ImuMotionEvent _new_event = ImuMotionEvent::None;
    bool _is_applied          = false;
    bool _restore_flag        = false;
};

}  // namespace stackchan
