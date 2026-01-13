/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "decorators.h"
#include <hal/hal.h>
#include <vector>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const Vector2i _angry_default_position        = Vector2i(108, -70);
static const lv_color_t _angry_default_color         = lv_color_hex(0xFDB034);
static const std::vector<int> _angry_rotation_frames = {150, 200};

LV_IMAGE_DECLARE(decorator_angry);

AngryDecorator::AngryDecorator(lv_obj_t* parent, uint32_t destroyAfterMs, uint32_t animationIntervalMs)
    : _animation_interval_ms(animationIntervalMs)
{
    // 初始化 UI 组件
    _angry = std::make_unique<Image>(parent);
    _angry->setSrc(&decorator_angry);
    _angry->setAlign(LV_ALIGN_CENTER);
    _angry->setPos(_angry_default_position.x, _angry_default_position.y);

    // 设置旋转中心和初始角度
    _angry->setTransformPivot(_angry->getWidth() / 2, _angry->getHeight() / 2);
    _angry->setRotation(_angry_rotation_frames[0]);

    // 设置颜色偏置
    _angry->setImageRecolorOpa(LV_OPA_COVER);
    _angry->setImageRecolor(_angry_default_color);

    uint32_t now = GetHAL().millis();

    // 初始化销毁倒计时
    if (destroyAfterMs > 0) {
        _destroy_at   = now + destroyAfterMs;
        _has_lifetime = true;
    }

    // 初始化动画倒计时
    if (_animation_interval_ms > 0) {
        _next_animation_tick = now + _animation_interval_ms;
    }
}

AngryDecorator::~AngryDecorator()
{
}

void AngryDecorator::_update()
{
    uint32_t now = GetHAL().millis();

    // 检查自动销毁
    if (_has_lifetime && now >= _destroy_at) {
        requestDestroy();
        return;
    }

    // 检查动画跳变
    if (_animation_interval_ms > 0 && now >= _next_animation_tick) {
        _next_animation_tick = now + _animation_interval_ms;

        // 切换帧
        _animation_index = (_animation_index + 1) % _angry_rotation_frames.size();
        _angry->setRotation(_angry_rotation_frames[_animation_index]);
    }
}

void AngryDecorator::setPosition(int x, int y)
{
    if (_angry) {
        _angry->setPos(x, y);
    }
}

void AngryDecorator::setRotation(int rotation)
{
    if (_angry) {
        _angry->setRotation(rotation);
    }
}

void AngryDecorator::setColor(lv_color_t color)
{
    if (_angry) {
        _angry->setImageRecolor(color);
    }
}
