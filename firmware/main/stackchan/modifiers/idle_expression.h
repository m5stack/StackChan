/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include "../utils/random.h"
#include <smooth_ui_toolkit.hpp>
#include <hal/hal.h>
#include <cstdint>

namespace stackchan {

/**
 * @brief
 *
 */
class IdleExpressionModifier : public Modifier {
public:
    IdleExpressionModifier(uint32_t interval_min = 2000, uint32_t interval_max = 6000)
        : _interval_min(interval_min), _interval_max(interval_max)
    {
        _next_tick = GetHAL().millis() + 500;
    }

    void _update(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) return;

        uint32_t now = GetHAL().millis();
        if (now < _next_tick) {
            return;
        }

        // 执行随机微表情
        perform_idle_emotion(stackchan.avatar());

        // 计算下一次触发时间
        uint32_t delay = Random::getInstance().getInt(_interval_min, _interval_max);
        _next_tick     = now + delay;
    }

private:
    void perform_idle_emotion(avatar::Avatar& avatar)
    {
        int action = Random::getInstance().getInt(0, 100);

        if (action < 70) {
            // 【动作 1：眼神游离】双眼协同移动一个小范围
            int offsetX = Random::getInstance().getInt(-20, 20);
            int offsetY = Random::getInstance().getInt(-15, 15);
            avatar.leftEye().setPosition({offsetX, offsetY});
            avatar.rightEye().setPosition({offsetX, offsetY});

            // 嘴巴也配合动一下
            avatar.mouth().setPosition({0, Random::getInstance().getInt(0, 10)});
        } else if (action < 80) {
            // 【动作 3：嘴巴歪一下】旋转角度
            // Rotation: 0~3600
            int rotation = Random::getInstance().getInt(-30, 30);
            // 加上基准值
            avatar.mouth().setRotation(rotation < 0 ? 3600 + rotation : rotation);
        } else {
            // 【动作 4：回归中性】
            reset_to_neutral(avatar);
        }
    }

    void reset_to_neutral(avatar::Avatar& avatar)
    {
        // 位置回归中心
        avatar.leftEye().setPosition({0, 0});
        avatar.rightEye().setPosition({0, 0});
        avatar.mouth().setPosition({0, 0});

        // 缩放回归正常
        avatar.leftEye().setSize(0);
        avatar.rightEye().setSize(0);

        // 旋转和权重回归
        avatar.mouth().setRotation(0);
        avatar.mouth().setWeight(0);
    }

    uint32_t _interval_min;
    uint32_t _interval_max;
    uint32_t _next_tick = 0;
};

}  // namespace stackchan
