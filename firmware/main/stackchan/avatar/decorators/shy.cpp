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

static const Vector2i _shy_left_default_position  = Vector2i(-108, 28);
static const Vector2i _shy_right_default_position = Vector2i(108, 28);
static const lv_color_t _shy_default_color        = lv_color_hex(0xF7A59E);

LV_IMAGE_DECLARE(decorator_shy);

ShyDecorator::ShyDecorator(lv_obj_t* parent, uint32_t destroyAfterMs)
{
    // Initialize Left Image
    _left = std::make_unique<Image>(parent);
    _left->setSrc(&decorator_shy);
    _left->setAlign(LV_ALIGN_CENTER);
    _left->setPos(_shy_left_default_position.x, _shy_left_default_position.y);
    _left->setTransformPivot(_left->getWidth() / 2, _left->getHeight() / 2);
    _left->setImageRecolorOpa(LV_OPA_COVER);
    _left->setImageRecolor(_shy_default_color);

    // Initialize Right Image
    _right = std::make_unique<Image>(parent);
    _right->setSrc(&decorator_shy);
    _right->setAlign(LV_ALIGN_CENTER);
    _right->setPos(_shy_right_default_position.x, _shy_right_default_position.y);
    _right->setTransformPivot(_right->getWidth() / 2, _right->getHeight() / 2);
    _right->setImageRecolorOpa(LV_OPA_COVER);
    _right->setImageRecolor(_shy_default_color);

    uint32_t now = GetHAL().millis();

    if (destroyAfterMs > 0) {
        _destroy_at   = now + destroyAfterMs;
        _has_lifetime = true;
    }
}

ShyDecorator::~ShyDecorator()
{
}

void ShyDecorator::_update()
{
    uint32_t now = GetHAL().millis();

    if (_has_lifetime && now >= _destroy_at) {
        requestDestroy();
        return;
    }
}

void ShyDecorator::setPosition(int x, int y)
{
    if (_left) {
        _left->setPos(x + _shy_left_default_position.x, y + _shy_left_default_position.y);
    }
    if (_right) {
        _right->setPos(x + _shy_right_default_position.x, y + _shy_right_default_position.y);
    }
}

void ShyDecorator::setRotation(int rotation)
{
    if (_left) {
        _left->setRotation(rotation);
    }
    if (_right) {
        _right->setRotation(rotation);
    }
}

void ShyDecorator::setColor(lv_color_t color)
{
    if (_left) {
        _left->setImageRecolor(color);
    }
    if (_right) {
        _right->setImageRecolor(color);
    }
}

void ShyDecorator::setVisible(bool visible)
{
    if (_left) {
        _left->setHidden(!visible);
    }
    if (_right) {
        _right->setHidden(!visible);
    }
}
