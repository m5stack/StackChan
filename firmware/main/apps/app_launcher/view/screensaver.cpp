/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <cstdint>
#include <memory>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace uitk::games;
using namespace uitk::games::dvd_screensaver;

static const Vector2 _screen_size               = {320, 240};
static const Vector2 _logo_size                 = {64, 48};
static const int _logo_id                       = 666;
static const std::vector<uint32_t> _logo_colors = {
    0xffffff, 0xfffa01, 0xff8300, 0x00feff, 0xff2600, 0xbe00ff, 0x0026ff, 0xff008b,
};
static const uint32_t _bg_color = 0x000000;

Screensaver::~Screensaver()
{
    _prev_screen->load();
}

void Screensaver::onInit()
{
    _prev_screen = std::make_unique<ScreenActive>();

    _screen = std::make_unique<Screen>();
    _screen->setBgColor(lv_color_hex(_bg_color));
    _screen->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _screen->setPadding(0, 0, 0, 0);
    _screen->load();

    _logo = std::make_unique<Container>(_screen->get());
    _logo->setSize(_logo_size.width, _logo_size.height);
    _logo->setBgColor(lv_color_hex(_logo_colors[0]));
    _logo->align(LV_ALIGN_TOP_LEFT, 2333, 2333);
    _logo->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _logo->setPadding(0, 0, 0, 0);
    _logo->setBorderWidth(0);
    _logo->setRadius(0);

    _left_eye = std::make_unique<Container>(_logo->get());
    _left_eye->align(LV_ALIGN_CENTER, -15, -3);
    _left_eye->setBgColor(lv_color_hex(_bg_color));
    _left_eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _left_eye->setRadius(LV_RADIUS_CIRCLE);
    _left_eye->setBorderWidth(0);
    _left_eye->setSize(6, 6);

    _right_eye = std::make_unique<Container>(_logo->get());
    _right_eye->align(LV_ALIGN_CENTER, 15, -3);
    _right_eye->setBgColor(lv_color_hex(_bg_color));
    _right_eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _right_eye->setRadius(LV_RADIUS_CIRCLE);
    _right_eye->setBorderWidth(0);
    _right_eye->setSize(6, 6);

    _mouth = std::make_unique<Container>(_logo->get());
    _mouth->align(LV_ALIGN_CENTER, 0, 5);
    _mouth->setBgColor(lv_color_hex(_bg_color));
    _mouth->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _mouth->setBorderWidth(0);
    _mouth->setSize(18, 2);
    _mouth->setRadius(0);
}

void Screensaver::onBuildLevel()
{
    addScreenFrameAsWall(_screen_size);

    auto& random      = Random::getInstance();
    Vector2 direction = {random.getFloat(0.3, 0.7), random.getFloat(0.3, 0.7)};
    direction         = direction.normalized();

    addLogo(_logo_id, {_screen_size.width / 2, _screen_size.height / 2}, _logo_size, direction, 110);
}

void Screensaver::onRender(float dt)
{
    getWorld().forEachObject([&](GameObject* obj) {
        if (obj->groupId == _logo_id) {
            auto p = obj->get<Transform>()->position;
            _logo->setPos((int)p.x - _logo_size.width / 2, (int)p.y - _logo_size.height / 2);
        }
    });
}

void Screensaver::onLogoCollide(int logoGroupId)
{
    _color_index++;
    if (_color_index >= _logo_colors.size()) {
        _color_index = 0;
    }

    _logo->setBgColor(lv_color_hex(_logo_colors[_color_index]));
}
