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

static const Vector2i _sweat_default_position     = Vector2i(-116, -72);
static const lv_color_t _sweat_default_color      = lv_color_hex(0x75E1FF);
static const std::vector<int> _sweat_pos_y_frames = {-72, -68, -62, -58, 0};

LV_IMAGE_DECLARE(decorator_sweat);

SweatDecorator::SweatDecorator(lv_obj_t* parent, uint32_t destroyAfterMs, uint32_t animationIntervalMs)
    : _animation_interval_ms(animationIntervalMs)
{
    // 初始化图像
    _sweat = std::make_unique<Image>(parent);
    _sweat->setSrc(&decorator_sweat);
    _sweat->setAlign(LV_ALIGN_CENTER);
    _sweat->setPos(_sweat_default_position.x, _sweat_default_position.y);

    _sweat->setTransformPivot(_sweat->getWidth() / 2, _sweat->getHeight() / 2);
    _sweat->setImageRecolorOpa(LV_OPA_COVER);
    _sweat->setImageRecolor(_sweat_default_color);

    uint32_t now = GetHAL().millis();

    if (destroyAfterMs > 0) {
        _destroy_at   = now + destroyAfterMs;
        _has_lifetime = true;
    }

    if (_animation_interval_ms > 0) {
        _next_animation_tick = now + _animation_interval_ms;
    }
}

SweatDecorator::~SweatDecorator()
{
}

void SweatDecorator::_update()
{
    uint32_t now = GetHAL().millis();

    // 检查自动销毁
    if (_has_lifetime && now >= _destroy_at) {
        requestDestroy();
        return;
    }

    // 检查动画帧更新
    if (_animation_interval_ms > 0 && now >= _next_animation_tick) {
        _next_animation_tick = now + _animation_interval_ms;

        int current_y_frame = _sweat_pos_y_frames[_animation_index];

        if (current_y_frame == 0) {
            // 特殊帧：隐藏图像
            setVisible(false);
        } else {
            // 普通帧：移动位置并显示
            setVisible(true);
            _sweat->setPos(_sweat_default_position.x, current_y_frame);
        }

        // 步进索引
        _animation_index = (_animation_index + 1) % _sweat_pos_y_frames.size();
    }
}

void SweatDecorator::setPosition(int x, int y)
{
    // 注意：这里的 setPosition 会覆盖动画中的 x 坐标
    if (_sweat) {
        _sweat->setPos(x, y);
    }
}

void SweatDecorator::setRotation(int rotation)
{
    if (_sweat) {
        _sweat->setRotation(rotation);
    }
}

void SweatDecorator::setColor(lv_color_t color)
{
    if (_sweat) {
        _sweat->setImageRecolor(color);
    }
}

void SweatDecorator::setVisible(bool visible)
{
    if (_sweat) {
        _sweat->setHidden(!visible);
    }
}
