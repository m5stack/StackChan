/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "motion_math.h"
#include <algorithm>
#include <cmath>

namespace stackchan::motion {

namespace {

constexpr float kRadiansToDegrees = 180.0f / static_cast<float>(M_PI);

float clampNormalized(float value)
{
    return std::clamp(value, -1.0f, 1.0f);
}

float mapRange(float value, float inMin, float inMax, float outMin, float outMax)
{
    if (inMax == inMin) {
        return outMin;
    }
    float ratio = (value - inMin) / (inMax - inMin);
    return outMin + ratio * (outMax - outMin);
}

int radiansToTenthsOfDegrees(float radians)
{
    return static_cast<int>(radians * kRadiansToDegrees * 10.0f);
}

}  // namespace

int mapNormalizedValueToAngle(float value, int minimum, int maximum)
{
    return static_cast<int>(mapRange(clampNormalized(value), -1.0f, 1.0f, static_cast<float>(minimum), static_cast<float>(maximum)));
}

MotionAngles calculateNormalizedLookAngles(float x, float y, int yawMin, int yawMax, int pitchMin, int pitchMax)
{
    return {
        .yaw = mapNormalizedValueToAngle(x, yawMin, yawMax),
        .pitch = mapNormalizedValueToAngle(y, pitchMin, pitchMax),
    };
}

MotionAngles calculatePointLookAngles(float x, float y, float z)
{
    float yawRadians = std::atan2(y, x);
    float groundDistance = std::sqrt(x * x + y * y);
    float pitchRadians = std::atan2(z, groundDistance);

    return {
        .yaw = radiansToTenthsOfDegrees(yawRadians),
        .pitch = radiansToTenthsOfDegrees(pitchRadians),
    };
}

}  // namespace stackchan::motion
