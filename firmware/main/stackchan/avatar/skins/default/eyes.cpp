/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "default.h"

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const int _eye_size            = 16;
static const Vector2i _eye_pos        = Vector2i(-70, -16);
static const Vector2i _eye_min_offset = Vector2i(-16, -16);
static const Vector2i _eye_max_offset = Vector2i(16, 16);
static const Vector2i _eye_size_limit = Vector2i(8, 32);

DefaultEyes::DefaultEyes(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor, bool isLeftEye)
{
    _is_left_eye = isLeftEye;

    _container = std::make_unique<Container>(parent);
    _container->setRadius(0);
    _container->setAlign(LV_ALIGN_CENTER);
    _container->setBorderWidth(0);
    _container->setBgOpa(0);
    _container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _container->setPadding(0, 0, 0, 0);
    _container->setTransformPivot(_eye_size_limit.y / 2, _eye_size_limit.y / 2);
    _container->setSize(_eye_size_limit.y, _eye_size_limit.y);

    _eye = std::make_unique<Container>(_container->get());
    _eye->setRadius(LV_RADIUS_CIRCLE);
    _eye->align(LV_ALIGN_CENTER, 0, 0);
    _eye->setBorderWidth(0);
    _eye->setBgColor(primaryColor);
    _eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _eyelid = std::make_unique<Container>(_container->get());
    _eyelid->setRadius(0);
    _eyelid->align(LV_ALIGN_CENTER, 0, 0);
    _eyelid->setBorderWidth(0);
    _eyelid->setBgColor(secondaryColor);
    _eyelid->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    setSize(0);
    setWeight(100);
    setPosition(_position);
    setRotation(0);
}

DefaultEyes::~DefaultEyes()
{
    _eyelid.reset();
    _eye.reset();
    _container.reset();
}

void DefaultEyes::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _is_left_eye ? _eye_pos.x : -_eye_pos.x;
    pos_x += map_range(_position.x, -100, 100, _eye_min_offset.x, _eye_max_offset.x);
    auto pos_y = _eye_pos.y + map_range(_position.y, -100, 100, _eye_min_offset.y, _eye_max_offset.y);

    _container->setPos(pos_x, pos_y);
    _eyelid->setY(_eyelid_offset_y);
}

void DefaultEyes::setWeight(int weight)
{
    Feature::setWeight(weight);

    _eyelid_offset_y = -map_range(_weight, 0, 100, 0, (int)_eyelid->getHeight());

    _eyelid->setY(_eyelid_offset_y);
}

void DefaultEyes::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _container->setRotation(rotation);
}

void DefaultEyes::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }

    auto apply_style = [this](int weight, int rotation) {
        setWeight(weight);
        if (_is_left_eye) {
            setRotation(rotation);
        } else {
            setRotation(-rotation);
        }
    };

    switch (emotion) {
        case Emotion::Neutral:
            apply_style(100, 0);
            break;
        case Emotion::Happy:
            apply_style(72, 1550);
            break;
        case Emotion::Angry:
            apply_style(70, 450);
            break;
        case Emotion::Sad:
            apply_style(70, -400);
            break;
        case Emotion::Doubt:
            apply_style(75, 0);
            break;
        case Emotion::Sleepy:
            apply_style(35, -50);
            break;
        default:
            break;
    }
}

void DefaultEyes::setVisible(bool visible)
{
    Element::setVisible(visible);

    _container->setHidden(!visible);
}

void DefaultEyes::setSize(int size)
{
    Feature::setSize(size);

    int eye_size = map_range(_size, -100, 100, _eye_size_limit.x, _eye_size_limit.y);

    _eye->setSize(eye_size, eye_size);
    _eyelid->setSize(eye_size, eye_size);

    // Force eyelid update
    setWeight(getWeight());
}
