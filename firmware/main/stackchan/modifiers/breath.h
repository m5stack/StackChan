/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <hal/hal.h>
#include <cstdint>
#include <cmath>

namespace stackchan {

/**
 * @brief
 *
 */
class BreathModifier : public Modifier {
public:
    /**
     * @param destroyAfterMs 持续时间（0 为永久）
     * @param amplitude 呼吸幅度，单位像素
     * @param breathCycleMs 呼吸一次的周期（吸+呼）
     * @param updateIntervalMs 更新间隔
     */
    BreathModifier(uint32_t destroyAfterMs = 0, int amplitude = 16, uint32_t breathCycleMs = 6600,
                   uint32_t updateIntervalMs = 600)
        : _amplitude(amplitude), _breath_cycle_ms(breathCycleMs), _update_interval_ms(updateIntervalMs)
    {
        _start_tick = GetHAL().millis();
        if (destroyAfterMs > 0) {
            _destroy_at   = _start_tick + destroyAfterMs;
            _has_lifetime = true;
        }
    }

    void _update(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) return;

        uint32_t now = GetHAL().millis();

        // 销毁逻辑
        if (_has_lifetime && now >= _destroy_at) {
            reset_position(stackchan.avatar());  // 销毁前复位
            requestDestroy();
            return;
        }

        if (now - _last_update_tick < _update_interval_ms) {
            return;
        }
        _last_update_tick = now;

        // 使用正弦波计算偏移量
        // (now - _start_tick) / cycle 得到进度，乘以 2PI 传给 sin
        float phase   = (float)((now - _start_tick) % _breath_cycle_ms) / _breath_cycle_ms;
        float sin_val = sinf(phase * 2.0f * M_PI);

        // 计算当前偏移
        int current_offset = static_cast<int>(sin_val * _amplitude);

        // 应用增量偏移
        apply_relative_offset(stackchan.avatar(), current_offset);
    }

private:
    void apply_relative_offset(avatar::Avatar& avatar, int new_offset)
    {
        // 计算本次需要移动的差值
        int delta = new_offset - _last_applied_offset;
        if (delta == 0) return;

        // 批量移动五官
        move_component(avatar.leftEye(), delta);
        move_component(avatar.rightEye(), delta);
        move_component(avatar.mouth(), delta);

        _last_applied_offset = new_offset;
    }

    void move_component(avatar::Feature& comp, int delta_y)
    {
        auto pos = comp.getPosition();
        pos.y += delta_y;
        comp.setPosition(pos);
    }

    void reset_position(avatar::Avatar& avatar)
    {
        // 将偏移归零
        apply_relative_offset(avatar, 0);
    }

    int _amplitude;
    uint32_t _breath_cycle_ms;
    uint32_t _update_interval_ms;
    uint32_t _start_tick       = 0;
    uint32_t _last_update_tick = 0;
    uint32_t _destroy_at       = 0;
    bool _has_lifetime         = false;

    int _last_applied_offset = 0;  // 记录上一次应用的偏移量，用于增量更新
};

}  // namespace stackchan
