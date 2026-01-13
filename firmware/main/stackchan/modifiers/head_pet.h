/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include "../avatar/decorators/decorators.h"
#include "../utils/random.h"
#include <smooth_ui_toolkit.hpp>
#include <hal/hal.h>
#include <cstdint>
#include <memory>

namespace stackchan {

/**
 * @brief
 *
 */
class HeadPetModifier : public Modifier {
public:
    HeadPetModifier(uint32_t restoreDelayMs = 3000) : _restore_delay_ms(restoreDelayMs)
    {
        // 绑定信号
        _signal_connection = GetHAL().onHeadPetGesture.connect([this](HeadPetGesture gesture) {
            if (gesture == HeadPetGesture::SwipeForward || gesture == HeadPetGesture::SwipeBackward) {
                _event_swipe = true;
            } else if (gesture == HeadPetGesture::Release) {
                _event_release = true;
            }
        });
    }

    ~HeadPetModifier()
    {
        GetHAL().onHeadPetGesture.disconnect(_signal_connection);
    }

    void _update(Modifiable& stackchan) override
    {
        uint32_t now = GetHAL().millis();

        // 处理“被抚摸中”事件
        if (_event_swipe) {
            _event_swipe = false;
            handle_swipe(stackchan);
            // 只要在摸，就推迟恢复时间
            _is_waiting_restore = false;
        }

        // 处理“手松开”事件
        if (_event_release) {
            _event_release = false;
            if (_in_happy_state) {
                _is_waiting_restore = true;
                _restore_tick       = now + _restore_delay_ms;
            }
        }

        // 处理恢复逻辑
        if (_is_waiting_restore && now >= _restore_tick) {
            _is_waiting_restore = false;
            restore_original_state(stackchan);
        }
    }

private:
    void handle_swipe(Modifiable& stackchan)
    {
        // 首次进入开心状态，记录原始信息
        if (!_in_happy_state) {
            _in_happy_state = true;
            _prev_emotion   = stackchan.avatar().getEmotion();
            auto angles     = stackchan.motion().getCurrentAngles();
            _prev_yaw       = angles.x;
            _prev_pitch     = angles.y;
        }

        // 视觉反馈
        stackchan.avatar().setEmotion(avatar::Emotion::Happy);

        // 添加爱心装饰
        int duration = Random::getInstance().getInt(1500, 2500);
        stackchan.avatar().removeDecorator(_heart_decorator_id);
        _heart_decorator_id = stackchan.avatar().addDecorator(
            std::make_unique<avatar::HeartDecorator>(lv_screen_active(), duration, 500));

        // 动作反馈
        perform_pet_motion(stackchan);
    }

    void restore_original_state(Modifiable& stackchan)
    {
        if (!_in_happy_state) {
            return;
        }

        stackchan.avatar().setEmotion(_prev_emotion);
        stackchan.motion().moveWithSpeed(_prev_yaw, _prev_pitch, 200);

        _in_happy_state = false;
    }

    void perform_pet_motion(Modifiable& stackchan)
    {
        auto& motion = stackchan.motion();
        int action   = Random::getInstance().getInt(0, 2);
        int speed    = Random::getInstance().getInt(100, 300);

        int32_t target_yaw   = _prev_yaw;
        int32_t target_pitch = _prev_pitch;

        switch (action) {
            case 0:  // 抬头
                target_pitch += Random::getInstance().getInt(150, 250);
                target_yaw += Random::getInstance().getInt(-50, 50);
                break;
            case 1:  // 歪头
                target_pitch -= Random::getInstance().getInt(0, 50);
                target_yaw += (Random::getInstance().getInt(0, 1) == 0 ? 150 : -150);
                break;
            case 2:  // 大幅度开心
                target_pitch += Random::getInstance().getInt(250, 400);
                break;
        }

        // 自然范围限制
        target_pitch = uitk::clamp(target_pitch, 0, 540);
        target_yaw   = uitk::clamp(target_yaw, -512, 512);

        motion.moveWithSpeed(target_yaw, target_pitch, speed);
    }

    // 信号相关
    int _signal_connection;
    volatile bool _event_swipe   = false;
    volatile bool _event_release = false;

    // 状态机相关
    bool _in_happy_state     = false;
    bool _is_waiting_restore = false;
    uint32_t _restore_tick   = 0;
    uint32_t _restore_delay_ms;
    int _heart_decorator_id = -1;

    // 记忆相关
    avatar::Emotion _prev_emotion = avatar::Emotion::Neutral;
    int32_t _prev_yaw             = 0;
    int32_t _prev_pitch           = 0;
};

}  // namespace stackchan
