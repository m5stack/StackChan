/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "timed.h"
#include "../avatar/decorators/decorators.h"
#include "../utils/random.h"
#include <smooth_ui_toolkit.hpp>
#include <hal/hal.h>
#include <cstdint>
#include <memory>

namespace stackchan {

class HeadPetModifier : public TimerModifier {
public:
    HeadPetModifier(uint32_t restoreDelayMs = 3000) : _restore_delay_ms(restoreDelayMs)
    {
        _signal_connection = GetHAL().onHeadPetGesture.connect([this](HeadPetGesture gesture) {
            LvglLockGuard lock;

            if (gesture == HeadPetGesture::SwipeForward || gesture == HeadPetGesture::SwipeBackward) {
                _trigger_happy   = true;
                _trigger_release = false;

                // 每次抚摸都打断恢复倒计时
                _should_restore       = false;
                _restore_timer_active = false;
            } else if (gesture == HeadPetGesture::Release) {
                _trigger_release = true;
            }
        });
    }

    ~HeadPetModifier()
    {
        GetHAL().onHeadPetGesture.disconnect(_signal_connection);
    }

    void _update(Modifiable& stackchan) override
    {
        TimerModifier::_update(stackchan);

        if (_trigger_happy) {
            _trigger_happy = false;

            // 1. 首次进入记录当前位置
            if (!_in_happy_state) {
                _prev_emotion   = stackchan.avatar().getEmotion();
                _prev_pitch     = stackchan.motion().getCurrentPitchAngle();
                _prev_yaw       = stackchan.motion().getCurrentYawAngle();
                _in_happy_state = true;
            }

            // 2. 表情反馈
            stackchan.avatar().setEmotion(avatar::Emotion::Happy);
            int duration = Random::getInstance().getInt(1500, 2500);
            stackchan.avatar().addDecorator(
                std::make_unique<avatar::HeartDecorator>(lv_screen_active(), duration, 500));

            // 3. 执行随机动作
            perform_random_motion(stackchan);
        }

        if (_trigger_release) {
            _trigger_release = false;
            if (_in_happy_state) {
                // 启动恢复计时
                _restore_timer_active = true;
                getTimer().addTask(_restore_delay_ms, 1, 0, [this]() { _should_restore = true; });
            }
        }

        if (_should_restore) {
            _should_restore = false;
            if (_restore_timer_active && _in_happy_state) {
                // 恢复原状
                stackchan.avatar().setEmotion(_prev_emotion);

                auto& motion = stackchan.motion();

                motion.moveWithSpeed(_prev_yaw, _prev_pitch, 200);

                _in_happy_state       = false;
                _restore_timer_active = false;
            }
        }
    }

private:
    void perform_random_motion(Modifiable& stackchan)
    {
        int action   = Random::getInstance().getInt(0, 2);
        auto& motion = stackchan.motion();

        int speed = Random::getInstance().getInt(400, 800);

        int32_t target_pitch = _prev_pitch;
        int32_t target_yaw   = _prev_yaw;

        // 角度限制: 10 units = 1度
        switch (action) {
            case 0:  // 舒服地蹭手/抬头
                target_pitch += Random::getInstance().getInt(150, 250);
                target_yaw += Random::getInstance().getInt(-50, 50);  // 微调 Yaw
                break;

            case 1:                                                   // 开心歪头/摇晃
                target_pitch -= Random::getInstance().getInt(0, 50);  // 略微低头
                if (Random::getInstance().getInt(0, 1) == 0) {
                    target_yaw += Random::getInstance().getInt(100, 200);
                } else {
                    target_yaw -= Random::getInstance().getInt(100, 200);
                }
                break;

            case 2:  // 兴奋大幅度抬头
                target_pitch += Random::getInstance().getInt(250, 400);
                break;
        }

        target_pitch = uitk::clamp(target_pitch, 0, 900);
        target_yaw   = uitk::clamp(target_yaw, -1280, 1280);

        motion.moveWithSpeed(target_yaw, target_pitch, speed);
    }

    int _signal_connection;
    uint32_t _restore_delay_ms;

    bool _trigger_happy   = false;
    bool _trigger_release = false;

    bool _in_happy_state       = false;
    bool _restore_timer_active = false;
    bool _should_restore       = false;

    avatar::Emotion _prev_emotion = avatar::Emotion::Neutral;
    int32_t _prev_pitch           = 0;
    int32_t _prev_yaw             = 0;
};

}  // namespace stackchan
