/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stackchan/motion/motion_math.h>

namespace {

using stackchan::motion::MotionAngles;
using stackchan::motion::calculateNormalizedLookAngles;
using stackchan::motion::calculatePointLookAngles;
using stackchan::motion::mapNormalizedValueToAngle;

void expectEqual(int actual, int expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

void expectAngles(MotionAngles actual, MotionAngles expected, const char* label)
{
    expectEqual(actual.yaw, expected.yaw, label);
    expectEqual(actual.pitch, expected.pitch, label);
}

void testNormalizedMapping()
{
    expectEqual(mapNormalizedValueToAngle(-1.0f, -1280, 1280), -1280, "normalized min");
    expectEqual(mapNormalizedValueToAngle(0.0f, -1280, 1280), 0, "normalized center");
    expectEqual(mapNormalizedValueToAngle(1.0f, -1280, 1280), 1280, "normalized max");
    expectEqual(mapNormalizedValueToAngle(2.0f, -1280, 1280), 1280, "normalized clamp high");
    expectEqual(mapNormalizedValueToAngle(-2.0f, 30, 870), 30, "normalized clamp low");
}

void testNormalizedLookAngles()
{
    expectAngles(
        calculateNormalizedLookAngles(0.5f, -0.5f, -1280, 1280, 30, 870),
        MotionAngles{640, 240},
        "normalized look"
    );
}

void testPointLookAngles()
{
    expectAngles(calculatePointLookAngles(1.0f, 0.0f, 0.0f), MotionAngles{0, 0}, "point forward");
    expectAngles(calculatePointLookAngles(0.0f, 1.0f, 0.0f), MotionAngles{900, 0}, "point left");
    expectAngles(calculatePointLookAngles(0.0f, -1.0f, 0.0f), MotionAngles{-900, 0}, "point right");

    MotionAngles diagonal = calculatePointLookAngles(1.0f, 1.0f, 1.0f);
    expectEqual(diagonal.yaw, 450, "point diagonal yaw");
    expectEqual(diagonal.pitch, 352, "point diagonal pitch");
}

}  // namespace

int main()
{
    testNormalizedMapping();
    testNormalizedLookAngles();
    testPointLookAngles();
    return 0;
}
