/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "animation.h"
#include "../stackchan.h"
#include <hal/hal.h>

using namespace stackchan::animation;

void Keyframe::apply()
{
    auto& stackchan = GetStackChan();
    auto& avatar    = stackchan.avatar();
    auto& motion    = stackchan.motion();

    auto apply_feature = [&](const FeatureKeyframe& kf, avatar::Feature& feat) {
        feat.setPosition(kf.position);
        feat.setRotation(kf.rotation);
        feat.setWeight(kf.weight);
        feat.setSize(kf.size);
    };
    apply_feature(leftEye, avatar.leftEye());
    apply_feature(rightEye, avatar.rightEye());
    apply_feature(mouth, avatar.mouth());

    auto apply_servo = [&](const ServoKeyframe& kf, motion::Servo& servo) { servo.moveWithSpeed(kf.angle, kf.speed); };
    apply_servo(yawServo, motion.yawServo());
    apply_servo(pitchServo, motion.pitchServo());

    stackchan.leftNeonLight().setColor(leftRgbColor);
    stackchan.rightNeonLight().setColor(rightRgbColor);
}

void Timeline::start()
{
    if (_keyframe_sequence.empty()) {
        _status = Status::Finished;
        return;
    }
    _current_index = 0;
    _status        = Status::Playing;
    _start_time    = GetHAL().millis();
    _apply_current_keyframe();
}

void Timeline::stop()
{
    _status        = Status::Idle;
    _current_index = 0;
}

void Timeline::pause()
{
    if (_status == Status::Playing) {
        _status       = Status::Paused;
        _elapsed_time = GetHAL().millis() - _start_time;
    }
}

void Timeline::resume()
{
    if (_status == Status::Paused) {
        _status     = Status::Playing;
        _start_time = GetHAL().millis() - _elapsed_time;
    }
}

void Timeline::update()
{
    if (_status != Status::Playing) {
        return;
    }

    if (_keyframe_sequence.empty()) {
        _status = Status::Finished;
        return;
    }

    uint32_t now = GetHAL().millis();
    if (now - _start_time >= _keyframe_sequence[_current_index].durationMs) {
        _current_index++;
        if (_current_index >= _keyframe_sequence.size()) {
            if (_loop) {
                _current_index = 0;
            } else {
                _status = Status::Finished;
                return;
            }
        }
        _start_time = now;
        _apply_current_keyframe();
    }
}

void Timeline::_apply_current_keyframe()
{
    if (_current_index < _keyframe_sequence.size()) {
        _keyframe_sequence[_current_index].apply();
    }
}
