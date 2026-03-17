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

static const Vector2i _heart_default_position        = Vector2i(108, -70);
static const lv_color_t _heart_default_color         = lv_color_hex(0xE13232);
static const std::vector<int> _heart_rotation_frames = {150, 200};

LV_IMAGE_DECLARE(decorator_heart);

HeartDecorator::HeartDecorator(lv_obj_t* parent, uint32_t destroyAfterMs, uint32_t animationIntervalMs)
    : _animation_interval_ms(animationIntervalMs)
{
    // 初始化图像
    _heart = std::make_unique<Image>(parent);
    _heart->setSrc(&decorator_heart);
    _heart->setAlign(LV_ALIGN_CENTER);
    _heart->setPos(_heart_default_position.x, _heart_default_position.y);

    // 设置旋转中心为中心点
    _heart->setTransformPivot(_heart->getWidth() / 2, _heart->getHeight() / 2);
    _heart->setRotation(_heart_rotation_frames[0]);

    // 设置颜色偏向
    _heart->setImageRecolorOpa(LV_OPA_COVER);
    _heart->setImageRecolor(_heart_default_color);

    uint32_t now = GetHAL().millis();

    // 设置销毁计时
    if (destroyAfterMs > 0) {
        _destroy_at   = now + destroyAfterMs;
        _has_lifetime = true;
    }

    // 设置动画计时
    if (_animation_interval_ms > 0) {
        _next_animation_tick = now + _animation_interval_ms;
    }
}

HeartDecorator::~HeartDecorator()
{
}

void HeartDecorator::_update()
{
    uint32_t now = GetHAL().millis();

    // 1. 处理销毁
    if (_has_lifetime && now >= _destroy_at) {
        requestDestroy();
        return;
    }

    // 2. 处理动画跳变（心跳效果）
    if (_animation_interval_ms > 0 && now >= _next_animation_tick) {
        _next_animation_tick = now + _animation_interval_ms;

        // 切换旋转角度
        _animation_index = (_animation_index + 1) % _heart_rotation_frames.size();
        _heart->setRotation(_heart_rotation_frames[_animation_index]);
    }
}

void HeartDecorator::setPosition(int x, int y)
{
    if (_heart) {
        _heart->setPos(x, y);
    }
}

void HeartDecorator::setRotation(int rotation)
{
    if (_heart) {
        _heart->setRotation(rotation);
    }
}

void HeartDecorator::setColor(lv_color_t color)
{
    if (_heart) {
        _heart->setImageRecolor(color);
    }
}
