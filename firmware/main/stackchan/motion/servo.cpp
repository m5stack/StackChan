/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo.h"
#include "servo_math.h"
#include <hal/hal.h>

using namespace uitk;

namespace stackchan::motion {

static SpringOptions_t _default_spring_options = {
    .stiffness = 170.0,
    .damping   = 26.0,

    .mass     = 1.0,
    .velocity = 0.0,

    .restSpeed = 0.1,
    .restDelta = 0.1,

    .duration       = 0.0,
    .bounce         = 0.0,
    .visualDuration = 0.0,
};

void Servo::init()
{
    apply_default_spring_options();

    _angle_anim.teleport(getCurrentAngle());
    update();

    setTorqueEnabled(false);
}

void Servo::update()
{
    // Keep update in at most 50Hz
    if (GetHAL().millis() - _last_tick < 20) {
        return;
    }
    _last_tick = GetHAL().millis();

    // Apply animation
    if (!_angle_anim.done()) {
        _angle_anim.updateWithDelta(0.02f);  // Fixed delta time for consistency
        set_angle_impl(static_cast<int>(_angle_anim.directValue()));
    }

    // Snap to target angle when animation ends
    else if (_snap_to_target_on_rest) {
        _snap_to_target_on_rest = false;
        set_angle_impl(_angle_anim.end);
    }

    // Auto release torque on rest
    else if (_auto_torque_release_enabled && !isMoving()) {
        if (GetHAL().millis() - _last_torque_check_tick > 200) {
            if (getTorqueEnabled()) {
                setTorqueEnabled(false);
            }
            _last_torque_check_tick = GetHAL().millis();
        }
    }
}

void Servo::move(int angle)
{
    apply_default_spring_options();
    update_angle_anim_target(angle);
}

void Servo::moveWithSpringParams(int angle, float stiffness, float damping)
{
    _angle_anim.springOptions().visualDuration = 0.0f;  // Disable timing override
    _angle_anim.springOptions().stiffness      = stiffness;
    _angle_anim.springOptions().damping        = damping;

    update_angle_anim_target(angle);
}

void Servo::moveWithSpeed(int angle, int speed)
{
    auto spring_options = map_speed_to_spring_options(speed);
    moveWithSpringParams(angle, spring_options.stiffness, spring_options.damping);
}

int Servo::getCurrentAngle()
{
    return _angle_anim.directValue();
}

bool Servo::isMoving()
{
    return _angle_anim.done() == false || is_moving_impl();
}

void Servo::apply_default_spring_options()
{
    auto& options          = _angle_anim.springOptions();
    options.visualDuration = 0.0f;  // Disable timing override
    options.stiffness      = _default_spring_options.stiffness;
    options.damping        = _default_spring_options.damping;
}

void Servo::update_angle_anim_target(int angle)
{
    angle = uitk::clamp(angle, _angle_limit.x, _angle_limit.y);

    if (_auto_angle_sync_enabled) {
        _angle_anim.teleport(getCurrentAngle());  // Use current angle as start
    }
    _angle_anim             = angle;  // Apply new target
    _snap_to_target_on_rest = true;
}

uitk::SpringOptions_t Servo::map_speed_to_spring_options(int speed)
{
    auto profile = calculateServoSpringProfile(speed);

    uitk::SpringOptions_t options = _default_spring_options;
    options.stiffness = profile.stiffness;
    options.damping = profile.damping;
    options.mass = profile.mass;
    options.restDelta = profile.restDelta;
    options.restSpeed = profile.restSpeed;

    return options;
}

}  // namespace stackchan::motion
