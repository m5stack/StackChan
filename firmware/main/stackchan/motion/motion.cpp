/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "motion.h"
#include "motion_math.h"

using namespace uitk;
using namespace stackchan::motion;

void Motion::init()
{
    _yaw_servo->init();
    _pitch_servo->init();
}

void Motion::update()
{
    _yaw_servo->update();
    _pitch_servo->update();
}

Servo& Motion::yawServo()
{
    return *_yaw_servo;
}

Servo& Motion::pitchServo()
{
    return *_pitch_servo;
}

void Motion::moveYaw(int angle)
{
    _yaw_servo->move(angle);
}

void Motion::moveYawWithSpeed(int angle, int speed)
{
    _yaw_servo->moveWithSpeed(angle, speed);
}

void Motion::movePitch(int angle)
{
    _pitch_servo->move(angle);
}

void Motion::movePitchWithSpeed(int angle, int speed)
{
    _pitch_servo->moveWithSpeed(angle, speed);
}

void Motion::move(int yawAngle, int pitchAngle)
{
    _yaw_servo->move(yawAngle);
    _pitch_servo->move(pitchAngle);
}

void Motion::moveWithSpeed(int yawAngle, int pitchAngle, int speed)
{
    _yaw_servo->moveWithSpeed(yawAngle, speed);
    _pitch_servo->moveWithSpeed(pitchAngle, speed);
}

void Motion::goHome(int speed)
{
    _yaw_servo->moveWithSpeed(0, speed);
    _pitch_servo->moveWithSpeed(0, speed);
}

void Motion::stop()
{
    _yaw_servo->move(_yaw_servo->getCurrentAngle());
    _pitch_servo->move(_pitch_servo->getCurrentAngle());
}

void Motion::lookAtNormalized(float x, float y, int speed)
{
    auto angles = calculateNormalizedLookAngles(
        x, y, _yaw_servo->getAngleLimit().x, _yaw_servo->getAngleLimit().y, _pitch_servo->getAngleLimit().x, _pitch_servo->getAngleLimit().y);
    moveWithSpeed(angles.yaw, angles.pitch, speed);
}

void Motion::lookAtPoint(float x, float y, float z, int speed)
{
    auto angles = calculatePointLookAngles(x, y, z);
    moveWithSpeed(angles.yaw, angles.pitch, speed);
}

bool Motion::isMoving()
{
    return _yaw_servo->isMoving() || _pitch_servo->isMoving();
}

int Motion::getCurrentYawAngle()
{
    return _yaw_servo->getCurrentAngle();
}

int Motion::getCurrentPitchAngle()
{
    return _pitch_servo->getCurrentAngle();
}

uitk::Vector2i Motion::getCurrentAngles()
{
    return uitk::Vector2i(_yaw_servo->getCurrentAngle(), _pitch_servo->getCurrentAngle());
}

void Motion::setTorqueEnabled(bool enabled)
{
    _yaw_servo->setTorqueEnabled(enabled);
    _pitch_servo->setTorqueEnabled(enabled);
}

void Motion::setAutoTorqueReleaseEnabled(bool enabled)
{
    _yaw_servo->setAutoTorqueReleaseEnabled(enabled);
    _pitch_servo->setAutoTorqueReleaseEnabled(enabled);
}

void Motion::setAutoAngleSyncEnabled(bool enabled)
{
    _yaw_servo->setAutoAngleSyncEnabled(enabled);
    _pitch_servo->setAutoAngleSyncEnabled(enabled);
}

void Motion::setModifyLock(bool locked)
{
    _is_modify_locked = locked;
}

bool Motion::isModifyLocked()
{
    return _is_modify_locked;
}
