/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "drivers/SCServo_lib/src/SCSCL.h"
#include <stackchan/stackchan.h>
#include <smooth_ui_toolkit.hpp>
#include <mooncake_log.h>
#include <settings.h>

using namespace smooth_ui_toolkit;
using namespace stackchan::motion;

static SCSCL _scs_bus;

class ScsServo : public Servo {
public:
    static inline const std::string _tag = "ScsServo";

    ScsServo(int id, Vector2i angleLimit, const std::string& settingNs, const std::string& zeroPosKey,
             bool enablePwmMode = false)
        : _id(id), _setting_ns(settingNs), _zero_pos_key(zeroPosKey), _enable_pwm_mode(enablePwmMode)
    {
        set_angle_limit(angleLimit);

        Settings settings(_setting_ns, false);
        _zero_pos = settings.GetInt(_zero_pos_key, 0);

        mclog::tagInfo(_tag, "id: {} get zero pos: {} from settings", _id, _zero_pos);
    }

    void set_angle_impl(int angle) override
    {
        int mapped_angle = _zero_pos + angle * 16 / 5 / 10;  // 一步对应 0.3125度, 0.3125 = 5/16
        if (mapped_angle < 0) {
            mapped_angle = 0;
        }

        // mclog::tagInfo(_tag, "id: {} mapped angle: {}", _id, mapped_angle);

        check_mode(Mode::Position);
        _scs_bus.WritePos(_id, mapped_angle, 20, 0);
    }

    int getCurrentAngle() override
    {
        int current_pos = _scs_bus.ReadPos(_id);
        int angle       = (current_pos - _zero_pos) * 5 * 10 / 16;
        angle           = uitk::clamp(angle, getAngleLimit().x, getAngleLimit().y);
        // mclog::tagInfo(_tag, "id: {} current pos: {} angle: {}", _id, current_pos, angle);
        return angle;
    }

    bool is_moving_impl() override
    {
        int moving = _scs_bus.ReadMove(_id);
        // mclog::tagInfo(_tag, "id: {} moving: {}", _id, moving);
        return moving != 0;
    }

    void setTorqueEnabled(bool enabled) override
    {
        Servo::setTorqueEnabled(enabled);
        _scs_bus.EnableTorque(_id, enabled ? 1 : 0);
        // mclog::tagInfo(_tag, "id: {} set torque: {}", _id, enabled);
    }

    bool getTorqueEnabled() override
    {
        int torque_enable = _scs_bus.ReadToqueEnable(_id);
        // mclog::tagInfo(_tag, "id: {} torque enable: {}", _id, torque_enable);
        return torque_enable > 0;
    }

    void setCurrentAngleAsZero() override
    {
        _zero_pos = _scs_bus.ReadPos(_id);

        Settings settings(_setting_ns, true);
        settings.SetInt(_zero_pos_key, _zero_pos);

        mclog::tagInfo(_tag, "id: {} set zero pos: {} to settings", _id, _zero_pos);
    }

    void rotate(int velocity) override
    {
        velocity = uitk::clamp(velocity, -1000, 1000);

        if (!_enable_pwm_mode) {
            return;
        }

        int mapped_velocity = map_range(velocity, 0, 1000, 0, 1023);

        check_mode(Mode::PWM);
        _scs_bus.WritePWM(_id, mapped_velocity);
    }

private:
    enum class Mode { Position = 0, PWM = 1 };

    int _id       = -1;
    int _zero_pos = 0;
    std::string _setting_ns;
    std::string _zero_pos_key;
    Mode _current_mode = Mode::Position;
    bool _enable_pwm_mode;

    uint32_t _last_true_move_ms     = 0;
    const uint32_t STOP_DEBOUNCE_MS = 250;

    void check_mode(Mode targetMode)
    {
        if (targetMode == _current_mode) {
            return;
        }

        _scs_bus.SwitchMode(_id, static_cast<uint8_t>(targetMode));
        _current_mode = targetMode;
    }
};

void Hal::servo_init()
{
    mclog::tagInfo("HAL-Servo", "init");

    _scs_bus.begin(UART_NUM_1, 1000000, 6, 7);

    auto motion =
        std::make_unique<Motion>(std::make_unique<ScsServo>(1, Vector2i(-1280, 1280), "servo", "zero_pos_1", true),
                                 std::make_unique<ScsServo>(2, Vector2i(0, 900), "servo", "zero_pos_2"));
    motion->init();

    GetStackChan().attachMotion(std::move(motion));
}
