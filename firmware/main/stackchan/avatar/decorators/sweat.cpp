/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "decorators.h"
#include <vector>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const Vector2i _sweat_default_position     = Vector2i(-116, -72);
static const lv_color_t _sweat_default_color      = lv_color_hex(0x75E1FF);
static const std::vector<int> _sweat_pos_y_frames = {-72, -68, -62, -58, 0};

LV_IMAGE_DECLARE(decorator_sweat);

SweatDecorator::SweatDecorator(lv_obj_t* parent, uint32_t destroyAfterMs, uint32_t animationIntervalMs)
{
    _sweat = std::make_unique<Image>(parent);
    _sweat->setSrc(&decorator_sweat);
    _sweat->setAlign(LV_ALIGN_CENTER);
    _sweat->setPos(_sweat_default_position.x, _sweat_default_position.y);
    _sweat->setTransformPivot(_sweat->getWidth() / 2, _sweat->getHeight() / 2);
    _sweat->setImageRecolorOpa(LV_OPA_COVER);
    _sweat->setImageRecolor(_sweat_default_color);

    if (destroyAfterMs != 0) {
        scheduleDestroy(destroyAfterMs);
    }

    if (animationIntervalMs != 0) {
        getTimer().addTask(animationIntervalMs, -1, 0, [this]() {
            if (_sweat_pos_y_frames[_animation_index] == 0) {
                setVisible(false);
            } else {
                setVisible(true);
                setPosition(_sweat_default_position.x, _sweat_pos_y_frames[_animation_index]);
            }
            _animation_index++;
            if (_animation_index >= _sweat_pos_y_frames.size()) {
                _animation_index = 0;
            }
        });
    }
}

SweatDecorator::~SweatDecorator()
{
    _sweat.reset();
}

void SweatDecorator::setPosition(int x, int y)
{
    Element::setPosition({x, y});
    _sweat->setPos(x, y);
}

void SweatDecorator::setRotation(int rotation)
{
    Element::setRotation(rotation);
    _sweat->setRotation(rotation);
}

void SweatDecorator::setColor(lv_color_t color)
{
    _sweat->setImageRecolor(color);
}

void SweatDecorator::setVisible(bool visible)
{
    Element::setVisible(visible);
    _sweat->setHidden(!visible);
}
