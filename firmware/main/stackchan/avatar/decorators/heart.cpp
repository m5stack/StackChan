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

static const Vector2i _heart_default_position        = Vector2i(108, -70);
static const lv_color_t _heart_default_color         = lv_color_hex(0xE13232);
static const std::vector<int> _heart_rotation_frames = {150, 200};

LV_IMAGE_DECLARE(decorator_heart);

HeartDecorator::HeartDecorator(lv_obj_t* parent, uint32_t destroyAfterMs, uint32_t animationIntervalMs)
{
    _heart = std::make_unique<Image>(parent);
    _heart->setSrc(&decorator_heart);
    _heart->setAlign(LV_ALIGN_CENTER);
    _heart->setPos(_heart_default_position.x, _heart_default_position.y);
    _heart->setTransformPivot(_heart->getWidth() / 2, _heart->getHeight() / 2);
    _heart->setRotation(_heart_rotation_frames[1]);
    _heart->setImageRecolorOpa(LV_OPA_COVER);
    _heart->setImageRecolor(_heart_default_color);

    if (destroyAfterMs != 0) {
        scheduleDestroy(destroyAfterMs);
    }

    if (animationIntervalMs != 0) {
        getTimer().addTask(animationIntervalMs, -1, 0, [this]() {
            setRotation(_heart_rotation_frames[_animation_index]);
            _animation_index++;
            if (_animation_index >= _heart_rotation_frames.size()) {
                _animation_index = 0;
            }
        });
    }
}

HeartDecorator::~HeartDecorator()
{
    _heart.reset();
}

void HeartDecorator::setPosition(int x, int y)
{
    _heart->setPos(x, y);
}

void HeartDecorator::setRotation(int rotation)
{
    _heart->setRotation(rotation);
}

void HeartDecorator::setColor(lv_color_t color)
{
    _heart->setImageRecolor(color);
}
