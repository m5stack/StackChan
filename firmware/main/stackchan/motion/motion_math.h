/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace stackchan::motion {

struct MotionAngles {
    int yaw;
    int pitch;
};

int mapNormalizedValueToAngle(float value, int minimum, int maximum);
MotionAngles calculateNormalizedLookAngles(float x, float y, int yawMin, int yawMax, int pitchMin, int pitchMax);
MotionAngles calculatePointLookAngles(float x, float y, float z);

}  // namespace stackchan::motion
