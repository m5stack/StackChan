/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "servo.h"
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <memory>

namespace stackchan::motion {

/**
 * @brief
 *
 */
class Motion {
public:
    Motion(std::unique_ptr<Servo> yawServo, std::unique_ptr<Servo> pitchServo)
        : _yaw_servo(std::move(yawServo)), _pitch_servo(std::move(pitchServo))
    {
    }

    /**
     * @brief
     *
     */
    void init();

    /**
     * @brief
     *
     */
    void update();

    /**
     * @brief Get yaw servo instance
     *
     * @return Servo&
     */
    Servo& yawServo();

    /**
     * @brief Get pitch servo instance
     *
     * @return Servo&
     */
    Servo& pitchServo();

    /**
     * @brief
     *
     * @param angle
     */
    void moveYaw(int angle);

    /**
     * @brief
     *
     * @param angle
     * @param speed (0-1000)
     */
    void moveYawWithSpeed(int angle, int speed);

    /**
     * @brief
     *
     * @param angle
     */
    void movePitch(int angle);

    /**
     * @brief
     *
     * @param angle
     * @param speed (0-1000)
     */
    void movePitchWithSpeed(int angle, int speed);

    /**
     * @brief
     *
     * @param yawAngle
     * @param pitchAngle
     */
    void move(int yawAngle, int pitchAngle);

    /**
     * @brief
     *
     * @param yawAngle
     * @param pitchAngle
     * @param speed (0-1000)
     */
    void moveWithSpeed(int yawAngle, int pitchAngle, int speed);

    /**
     * @brief Move head to home position (0,0)
     *
     * @param speed (0-1000)
     */
    void goHome(int speed = 500);

    /**
     * @brief Stop head movement
     *
     */
    void stop();

    /**
     * @brief Moves the head using normalized coordinates ranging from -1.0 to 1.0.
     *
     * This method maps a proportional input to the full physical range of the servos.
     * It is ideal for visual tracking (e.g., centering a face in a camera frame)
     * or joystick-based control.
     *
     * Mapping Logic:
     * - X-axis (Yaw): -1.0 (Max Left) <---> 1.0 (Max Right). 0.0 is center.
     * - Y-axis (Pitch): -1.0 (Max Down) <---> 1.0 (Max Up). 0.0 is the midpoint of the pitch range.
     *
     * @param x Normalized horizontal value [-1.0, 1.0].
     * @param y Normalized vertical value [-1.0, 1.0].
     * @param speed Movement speed from 0 to 1000.
     *
     * @note The actual angles are calculated based on the servo's `getAngleLimit()`.
     *       For example, if Pitch range is 0 to 900, y = -1.0 maps to 0 and y = 1.0 maps to 900.
     */
    void lookAtNormalized(float x, float y, int speed = 500);

    /**
     * @brief Directs the head to look at a target point in 3D Cartesian space.
     *
     * This method performs Inverse Kinematics (IK) to convert 3D coordinates
     * into Yaw and Pitch angles. It assumes the head rotation center is at (0,0,0).
     *
     * Coordinate System (Right-Handed):
     * - X-axis: Forward (positive is in front of the robot).
     * - Y-axis: Lateral (positive is to the left, negative is to the right).
     * - Z-axis: Vertical (positive is up, negative is down).
     *
     * @param x The forward distance from the rotation center (usually in mm).
     * @param y The lateral distance; positive values move the head left (usually in mm).
     * @param z The vertical distance; positive values move the head up (usually in mm).
     * @param speed The movement speed, ranging from 0 (slowest) to 1000 (fastest).
     *
     * @note If the target point is at (0,0,0), the behavior is undefined (mathematical singularity).
     */
    void lookAtPoint(float x, float y, float z, int speed = 500);

    bool isMoving();
    uitk::Vector2i getCurrentAngles();
    int getCurrentYawAngle();
    int getCurrentPitchAngle();
    void setTorqueEnabled(bool enabled);
    void setAutoTorqueReleaseEnabled(bool enabled);
    void setAutoAngleSyncEnabled(bool enabled);

private:
    std::unique_ptr<Servo> _yaw_servo;
    std::unique_ptr<Servo> _pitch_servo;

    static constexpr float RAD_TO_DEG = 180.0f / M_PI;

    inline float to_degrees(float radians)
    {
        return radians * RAD_TO_DEG;
    }
};

}  // namespace stackchan::motion
