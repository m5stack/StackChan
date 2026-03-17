/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <cstdint>

namespace stackchan::motion {

/**
 * @brief
 *
 */
class Servo {
public:
    virtual ~Servo() = default;

    /**
     * @brief
     *
     */
    virtual void init();

    /**
     * @brief
     *
     */
    virtual void update();

    /**
     * @brief Move to angle
     *
     * @param angle
     */
    void move(int angle);

    /**
     * @brief Move to angle with custom spring params
     *
     * @param angle
     * @param stiffness
     * @param damping
     */
    void moveWithSpringParams(int angle, float stiffness = 170.0f, float damping = 26.0f);

    /**
     * @brief Move to angle with speed mapping
     *
     * @param angle
     * @param speed (0-1000)
     */
    void moveWithSpeed(int angle, int speed);

    /**
     * @brief Rotate servo with given velocity
     *
     * @param velocity (-1000, 1000)
     */
    virtual void rotate(int velocity)
    {
    }

    /**
     * @brief Get servo current angle
     *
     * @return int
     */
    virtual int getCurrentAngle();

    /**
     * @brief
     *
     * @return uitk::Vector2i
     */
    virtual uitk::Vector2i getAngleLimit() const
    {
        return _angle_limit;
    }

    /**
     * @brief
     *
     * @return true
     * @return false
     */
    bool isMoving();

    /**
     * @brief
     *
     * @param enabled
     */
    virtual void setTorqueEnabled(bool enabled)
    {
    }
    virtual bool getTorqueEnabled()
    {
        return false;
    }

    /**
     * @brief Auto release torque on rest
     *
     * @param enabled
     */
    void setAutoTorqueReleaseEnabled(bool enabled)
    {
        _auto_torque_release_enabled = enabled;
    }

    /**
     * @brief Enables or disables automatic synchronization of the animation start point
     *        with the current physical angle.
     *
     * @param enabled
     *        - If true: Prevents sudden "jumps" when the servo is moved manually by
     *          external forces, but may cause stuttering during high-frequency updates
     *          as it resets the animation's velocity.
     *        - If false: Maintains animation momentum and velocity for smooth,
     *          continuous motion, but may cause a "snap" if the actual angle differs
     *          significantly from the internal state.
     */
    void setAutoAngleSyncEnabled(bool enabled)
    {
        _auto_angle_sync_enabled = enabled;
    }

    /**
     * @brief
     *
     */
    virtual void setCurrentAngleAsZero()
    {
    }

    /**
     * @brief
     *
     */
    virtual void resetZeroCalibration()
    {
    }

protected:
    Servo()
    {
    }

    void set_angle_limit(uitk::Vector2i angleLimit)
    {
        _angle_limit = angleLimit;
    }

    /**
     * @brief Servo set angle implementation
     *
     * @param angle
     */
    virtual void set_angle_impl(int angle) = 0;
    virtual bool is_moving_impl()
    {
        return false;
    }

protected:
    uitk::Vector2i _angle_limit;
    uitk::AnimateValue _angle_anim;

    uint32_t _last_tick               = 0;
    uint32_t _last_torque_check_tick  = 0;
    bool _snap_to_target_on_rest      = false;
    bool _auto_torque_release_enabled = true;
    bool _auto_angle_sync_enabled     = true;

    void apply_default_spring_options();
    void update_angle_anim_target(int angle);
    uitk::SpringOptions_t map_speed_to_spring_options(int speed);
};

}  // namespace stackchan::motion
