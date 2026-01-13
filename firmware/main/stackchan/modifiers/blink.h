/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include "../utils/random.h"
#include <hal/hal.h>
#include <cstdint>

namespace stackchan {

/**
 * @brief
 *
 */
class BlinkModifier : public Modifier {
public:
    /**
     * @param destroyAfterMs 持续多久后停止眨眼并销毁（0 为永久）
     * @param openIntervalMs 睁眼持续时间
     * @param closeIntervalMs 闭眼持续时间（瞬间）
     */
    BlinkModifier(uint32_t destroyAfterMs = 0, uint32_t openIntervalMs = 5200, uint32_t closeIntervalMs = 200)
        : _open_interval_ms(openIntervalMs), _close_interval_ms(closeIntervalMs)
    {
        uint32_t now = GetHAL().millis();

        // 处理销毁计时
        if (destroyAfterMs > 0) {
            _destroy_at   = now + destroyAfterMs;
            _has_lifetime = true;
        }

        // 初始化：从睁眼状态开始，立即准备闭眼
        _state           = State::OPEN;
        _next_state_tick = now + _open_interval_ms;
    }

    void resyncEyeWeights()
    {
        _needs_resync = true;
    }

    void _update(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        uint32_t now = GetHAL().millis();

        // 1. 处理销毁逻辑
        if (_has_lifetime && now >= _destroy_at) {
            // 销毁前确保眼睛是睁开的
            if (_state == State::CLOSED) {
                apply_eye_weights(stackchan, _left_eye_weight, _right_eye_weight);
            }
            requestDestroy();
            return;
        }

        // 2. 处理权重同步请求
        // 如果眼睛正闭着，我们只记录权重，等睁眼时再应用
        if (_needs_resync) {
            _needs_resync     = false;
            _left_eye_weight  = stackchan.avatar().leftEye().getWeight();
            _right_eye_weight = stackchan.avatar().rightEye().getWeight();
        }

        // 3. 状态机切换逻辑
        if (now >= _next_state_tick) {
            if (_state == State::OPEN) {
                // 睁眼 -> 闭眼
                _state           = State::CLOSED;
                _next_state_tick = now + _close_interval_ms;

                // 闭眼瞬间，先备份当前权重（以防外部中途修改了权重）
                _left_eye_weight  = stackchan.avatar().leftEye().getWeight();
                _right_eye_weight = stackchan.avatar().rightEye().getWeight();

                apply_eye_weights(stackchan, 25, 25);
            } else {
                // 闭眼 -> 睁眼
                _state = State::OPEN;
                // 睁眼时间可以加一点随机抖动，看起来更自然
                uint32_t jitter  = Random::getInstance().getInt(0, 500);
                _next_state_tick = now + _open_interval_ms + jitter;

                apply_eye_weights(stackchan, _left_eye_weight, _right_eye_weight);
            }
        }
    }

private:
    enum class State { OPEN, CLOSED };

    void apply_eye_weights(Modifiable& stackchan, int left, int right)
    {
        stackchan.avatar().leftEye().setWeight(left);
        stackchan.avatar().rightEye().setWeight(right);
    }

    State _state;
    uint32_t _next_state_tick = 0;
    uint32_t _open_interval_ms;
    uint32_t _close_interval_ms;

    uint32_t _destroy_at  = 0;
    bool _has_lifetime    = false;
    bool _needs_resync    = false;
    int _left_eye_weight  = 100;
    int _right_eye_weight = 100;
};

}  // namespace stackchan
