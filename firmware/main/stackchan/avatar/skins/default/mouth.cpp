/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "default.h"

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const Vector2i _mouth_pos        = Vector2i(0, 26);
static const Vector2i _mouth_min_offset = Vector2i(-16, -16);
static const Vector2i _mouth_max_offset = Vector2i(16, 16);
static const Vector2i _mouth_min_size   = Vector2i(90, 6);
static const Vector2i _mouth_max_size   = Vector2i(60, 50);
static const int _mouth_min_radius      = 0;
static const int _mouth_max_radius      = 16;

DefaultMouth::DefaultMouth(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor)
{
    _mouth = std::make_unique<Container>(parent);
    _mouth->setAlign(LV_ALIGN_CENTER);
    _mouth->setBorderWidth(0);
    _mouth->setBgColor(primaryColor);
    _mouth->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    setPosition(_position);
    setWeight(0);
    setRotation(0);
}

DefaultMouth::~DefaultMouth()
{
    _mouth.reset();
}

void DefaultMouth::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _mouth_pos.x + map_range(_position.x, -100, 100, _mouth_min_offset.x, _mouth_max_offset.x);
    auto pos_y = _mouth_pos.y + map_range(_position.y, -100, 100, _mouth_min_offset.y, _mouth_max_offset.y);

    _mouth->setPos(pos_x, pos_y);
}

void DefaultMouth::setWeight(int weight)
{
    Feature::setWeight(weight);

    auto size_x = map_range(_weight, 0, 100, _mouth_min_size.x, _mouth_max_size.x);
    auto size_y = map_range(_weight, 0, 100, _mouth_min_size.y, _mouth_max_size.y);
    auto radius = map_range(_weight, 0, 100, _mouth_min_radius, _mouth_max_radius);

    _mouth->setSize(size_x, size_y);
    _mouth->setRadius(radius);
}

void DefaultMouth::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _mouth->setTransformPivot(_mouth->getWidth() / 2, _mouth->getHeight() / 2);
    _mouth->setRotation(rotation);
}

void DefaultMouth::setVisible(bool visible)
{
    Element::setVisible(visible);

    _mouth->setHidden(!visible);
}
