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

static const Vector2i _angry_default_position        = Vector2i(108, -70);
static const lv_color_t _angry_default_color         = lv_color_hex(0xFDB034);
static const std::vector<int> _angry_rotation_frames = {150, 200};

LV_IMAGE_DECLARE(decorator_angry);

AngryDecorator::AngryDecorator(lv_obj_t* parent, uint32_t destroyAfterMs, uint32_t animationIntervalMs)
{
    _angry = std::make_unique<Image>(parent);
    _angry->setSrc(&decorator_angry);
    _angry->setAlign(LV_ALIGN_CENTER);
    _angry->setPos(_angry_default_position.x, _angry_default_position.y);
    _angry->setTransformPivot(_angry->getWidth() / 2, _angry->getHeight() / 2);
    _angry->setRotation(_angry_rotation_frames[1]);
    _angry->setImageRecolorOpa(LV_OPA_COVER);
    _angry->setImageRecolor(_angry_default_color);

    if (destroyAfterMs != 0) {
        scheduleDestroy(destroyAfterMs);
    }

    if (animationIntervalMs != 0) {
        getTimer().addTask(animationIntervalMs, -1, 0, [this]() {
            setRotation(_angry_rotation_frames[_animation_index]);
            _animation_index++;
            if (_animation_index >= _angry_rotation_frames.size()) {
                _animation_index = 0;
            }
        });
    }
}

AngryDecorator::~AngryDecorator()
{
    _angry.reset();
}

void AngryDecorator::setPosition(int x, int y)
{
    _angry->setPos(x, y);
}

void AngryDecorator::setRotation(int rotation)
{
    _angry->setRotation(rotation);
}

void AngryDecorator::setColor(lv_color_t color)
{
    _angry->setImageRecolor(color);
}
