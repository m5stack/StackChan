/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_math.h"
#include <algorithm>
#include <cmath>

namespace stackchan::motion {

int clampServoSpeed(int speed)
{
    return std::clamp(speed, 0, 1000);
}

ServoSpringProfile calculateServoSpringProfile(int speed)
{
    speed = clampServoSpeed(speed);

    float kMin = 10.0f;
    float kMax = 650.0f;
    float normalizedSpeed = speed / 1000.0f;
    float stiffness = kMin + (normalizedSpeed * normalizedSpeed) * (kMax - kMin);

    float mass = 1.0f;
    float damping = 2.0f * std::sqrt(mass * stiffness);

    ServoSpringProfile profile{
        .stiffness = stiffness,
        .damping = damping,
        .mass = mass,
        .restSpeed = 0.1f,
        .restDelta = 0.1f,
    };

    if (speed > 800) {
        profile.restDelta = 0.5f;
        profile.restSpeed = 0.5f;
    }

    return profile;
}

}  // namespace stackchan::motion
