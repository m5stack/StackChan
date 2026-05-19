/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace stackchan::motion {

struct ServoSpringProfile {
    float stiffness;
    float damping;
    float mass;
    float restSpeed;
    float restDelta;
};

int clampServoSpeed(int speed);
ServoSpringProfile calculateServoSpringProfile(int speed);

}  // namespace stackchan::motion
