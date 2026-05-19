/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stackchan/motion/servo_math.h>

namespace {

using stackchan::motion::ServoSpringProfile;
using stackchan::motion::calculateServoSpringProfile;
using stackchan::motion::clampServoSpeed;

void expectEqual(int actual, int expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

void expectNear(float actual, float expected, float tolerance, const char* label)
{
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

void testSpeedClamp()
{
    expectEqual(clampServoSpeed(-10), 0, "speed clamp low");
    expectEqual(clampServoSpeed(500), 500, "speed clamp mid");
    expectEqual(clampServoSpeed(1200), 1000, "speed clamp high");
}

void testProfileAtDefaultMidSpeed()
{
    ServoSpringProfile profile = calculateServoSpringProfile(500);
    expectNear(profile.stiffness, 170.0f, 0.01f, "mid stiffness");
    expectNear(profile.damping, 26.0768f, 0.01f, "mid damping");
    expectNear(profile.mass, 1.0f, 0.0f, "mid mass");
    expectNear(profile.restSpeed, 0.1f, 0.0f, "mid rest speed");
    expectNear(profile.restDelta, 0.1f, 0.0f, "mid rest delta");
}

void testProfileAtHighSpeedUsesRelaxedRestThresholds()
{
    ServoSpringProfile profile = calculateServoSpringProfile(900);
    expectNear(profile.stiffness, 528.4f, 0.01f, "high stiffness");
    expectNear(profile.damping, 45.974f, 0.02f, "high damping");
    expectNear(profile.restSpeed, 0.5f, 0.0f, "high rest speed");
    expectNear(profile.restDelta, 0.5f, 0.0f, "high rest delta");
}

void testProfileClampMatchesEndpoints()
{
    ServoSpringProfile low = calculateServoSpringProfile(-1);
    ServoSpringProfile high = calculateServoSpringProfile(1001);
    expectNear(low.stiffness, 10.0f, 0.0f, "low stiffness");
    expectNear(high.stiffness, 650.0f, 0.0f, "high stiffness");
}

}  // namespace

int main()
{
    testSpeedClamp();
    testProfileAtDefaultMidSpeed();
    testProfileAtHighSpeedUsesRelaxedRestThresholds();
    testProfileClampMatchesEndpoints();
    return 0;
}
