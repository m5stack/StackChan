/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// Ref: https://dribbble.com/shots/21953371-WeStud-Creative-Log-In-For-The-Educational-Platform
// Idea by cxbbb
#include "workers.h"
#include <stackchan/stackchan.h>
#include <cstdint>
#include <memory>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <functional>
#include <vector>
#include <apps/common/loading_page/loading_page.h>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-About";

class Egg {
public:
    class Face {
    public:
        Face(lv_obj_t* parent, const Vector2i& position, float stiffness, float damping, int bodyHeight)
        {
            const Vector2 logo_size = {64, 48};
            const uint32_t color    = 0x36064D;

            _position = position;

            _logo = std::make_unique<Container>(parent);
            _logo->setSize(logo_size.width, logo_size.height);
            _logo->align(LV_ALIGN_TOP_LEFT, position.x, position.y);
            _logo->setBgOpa(LV_OPA_TRANSP);
            _logo->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _logo->setPaddingAll(0);
            _logo->setBorderWidth(0);
            _logo->setRadius(0);

            _left_eye = std::make_unique<Container>(_logo->get());
            _left_eye->align(LV_ALIGN_CENTER, -15, -3);
            _left_eye->setBgColor(lv_color_hex(color));
            _left_eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _left_eye->setRadius(LV_RADIUS_CIRCLE);
            _left_eye->setBorderWidth(0);
            _left_eye->setSize(6, 6);

            _right_eye = std::make_unique<Container>(_logo->get());
            _right_eye->align(LV_ALIGN_CENTER, 15, -3);
            _right_eye->setBgColor(lv_color_hex(color));
            _right_eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _right_eye->setRadius(LV_RADIUS_CIRCLE);
            _right_eye->setBorderWidth(0);
            _right_eye->setSize(6, 6);

            _mouth = std::make_unique<Container>(_logo->get());
            _mouth->align(LV_ALIGN_CENTER, 0, 5);
            _mouth->setBgColor(lv_color_hex(color));
            _mouth->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _mouth->setBorderWidth(0);
            _mouth->setSize(18, 2);
            _mouth->setRadius(0);

            _look_at_anim.x.springOptions().stiffness = stiffness;
            _look_at_anim.x.springOptions().damping   = damping;
            _look_at_anim.y.springOptions()           = _look_at_anim.x.springOptions();

            _look_at_anim.teleport(_position.x, _position.y + bodyHeight);
            update();
        }

        void lookAt(float x, float y, bool instant = false)
        {
            // x, y are expected to be in range [-1, 1]
            const float max_x_offset = 17.0f;
            const float max_y_offset = 13.0f;

            float target_x = _position.x + x * max_x_offset;
            float target_y = _position.y + y * max_y_offset;

            if (instant) {
                _logo->setPos(target_x, target_y);
                _look_at_anim.teleport(target_x, target_y);
            } else {
                _look_at_anim.move(target_x, target_y);
            }
        }

        void update()
        {
            _look_at_anim.update();
            if (!_look_at_anim.done()) {
                _logo->setPos(_look_at_anim.directValue().x, _look_at_anim.directValue().y);
            }

            // Update blink
            uint32_t now = GetHAL().millis();
            if (_next_blink_time == 0) {
                _next_blink_time = now + Random::getInstance().getInt(100, 3000);
            }

            if (now > _next_blink_time) {
                if (_is_blinking) {
                    _is_blinking = false;
                    _left_eye->setHidden(false);
                    _right_eye->setHidden(false);
                    _next_blink_time = now + Random::getInstance().getInt(2000, 6000);
                } else {
                    _is_blinking = true;
                    _left_eye->setHidden(true);
                    _right_eye->setHidden(true);
                    _next_blink_time = now + Random::getInstance().getInt(60, 200);
                }
            }
        }

    private:
        std::unique_ptr<Container> _logo;
        std::unique_ptr<Container> _left_eye;
        std::unique_ptr<Container> _right_eye;
        std::unique_ptr<Container> _mouth;
        Vector2i _position;
        AnimateVector2 _look_at_anim;
        uint32_t _next_blink_time = 0;
        bool _is_blinking         = false;
    };

    class Cube {
    public:
        struct MetaData_t {
            Vector2i position;
            Vector2i size;
            uint32_t color = 0;
            Vector2i facePosition;
            float stiffness = 200.0f;
            float damping   = 20.0f;
        };

        Cube(lv_obj_t* parent, const MetaData_t& metaData)
        {
            _position      = metaData.position;
            _face_position = metaData.facePosition;

            _body = std::make_unique<Container>(parent);
            _body->setBgColor(lv_color_hex(metaData.color));
            _body->align(LV_ALIGN_TOP_LEFT, metaData.position.x, metaData.position.y);
            _body->setBorderWidth(0);
            _body->setSize(metaData.size.x, metaData.size.y);
            _body->setRadius(0);
            _body->setPaddingAll(0);

            _face = std::make_unique<Face>(parent, metaData.facePosition, metaData.stiffness, metaData.damping,
                                           metaData.size.y);

            _body_anim.x.springOptions().stiffness = metaData.stiffness;
            _body_anim.x.springOptions().damping   = metaData.damping;
            _body_anim.y.springOptions()           = _body_anim.x.springOptions();

            _body_anim.teleport(_position.x, _position.y + metaData.size.y);
            moveTo(1, 0);
        }

        void update(int tpX, int tpY)
        {
            auto target = getTiltTarget(tpX, tpY);

            // Update face anim
            _face->lookAt(target.x, target.y);
            _face->update();

            // Update body anim
            moveTo(target.x, target.y);
            _body_anim.update();
            if (!_body_anim.done()) {
                _body->setPos(_body_anim.directValue().x, _body_anim.directValue().y);
            }
        }

        Vector2 getTiltTarget(int tpX, int tpY)
        {
            Vector2 position;

            if (tpX < 0 || tpY < 0) {
                position.x = 1.0f;
                position.y = 0.0f;
            } else {
                // Face center
                float cx = _face_position.x + 32.0f;
                float cy = _face_position.y + 24.0f;

                float x = (tpX - cx) / 160.0f;
                float y = (tpY - cy) / 120.0f;

                position.x = uitk::clamp(x, -1.0f, 1.0f);
                position.y = uitk::clamp(y, -1.0f, 1.0f);
            }

            return position;
        }

        void moveTo(float x, float y, bool instant = false)
        {
            // x, y are expected to be in range [-1, 1]
            const float max_x_offset = 5.0f;
            const float max_y_offset = 10.0f;

            float target_x = _position.x + x * max_x_offset;
            float target_y = _position.y + y * max_y_offset;

            if (instant) {
                _body->setPos(target_x, target_y);
                _body_anim.teleport(target_x, target_y);
            } else {
                _body_anim.move(target_x, target_y);
            }
        }

    private:
        std::unique_ptr<Container> _body;
        std::unique_ptr<Face> _face;
        Vector2i _position;
        Vector2i _face_position;
        AnimateVector2 _body_anim;
    };

    inline static const std::vector<Cube::MetaData_t> CubeDefines = {
        {{54, 55}, {121, 185 + 10}, 0xBE70E5, {83, 55}, 50.0f, 14.0f},       //
        {{107, 131}, {103, 109 + 10}, 0xDA4848, {133, 131}, 100.0f, 16.0f},  //
        {{25, 166}, {89, 74 + 10}, 0x76D2DB, {37, 164}, 200.0f, 28.0f},      //
        {{136, 199}, {121, 42 + 10}, 0xF5AA79, {178, 196}, 25.0f, 10.0f},    //
    };

    void init()
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0xF7F6E5));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);
        _panel->setPaddingAll(0);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        for (const auto& metaData : CubeDefines) {
            _cubes.push_back(std::make_unique<Cube>(_panel->get(), metaData));
        }
    }

    void update()
    {
        int tpX = -1;
        int tpY = -1;

        lv_indev_t* indev = GetHAL().lvTouchpad;
        if (indev) {
            lv_indev_state_t state = lv_indev_get_state(indev);
            if (state == LV_INDEV_STATE_PR) {
                lv_point_t curr_point;
                lv_indev_get_point(indev, &curr_point);
                tpX = curr_point.x;
                tpY = curr_point.y;
            }
        }

        for (auto& cube : _cubes) {
            cube->update(tpX, tpY);
        }
    }

private:
    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Cube>> _cubes;
};
std::unique_ptr<Egg> _egg;

FwVersionWorker::FwVersionWorker()
{
    _egg = std::make_unique<Egg>();
    _egg->init();
}

FwVersionWorker::~FwVersionWorker()
{
    _egg.reset();
}

void FwVersionWorker::update()
{
    if (GetHAL().millis() - _last_tick > 16) {
        _last_tick = GetHAL().millis();
        _egg->update();
    }
}

SystemUpdateWorker::SystemUpdateWorker()
{
    auto loading_page = std::make_unique<view::LoadingPage>(0xF6F6F6, 0x26206A);
    GetHAL().lvglUnlock();

    // Start network
    GetHAL().startNetwork([&](std::string_view msg) {
        LvglLockGuard lock;
        loading_page->setMessage(msg);
    });

    // Update Firmware
    bool result = GetHAL().updateFirmware([&](std::string_view msg) {
        LvglLockGuard lock;
        loading_page->setMessage(msg);
    });

    // Hold the result for a while
    GetHAL().delay(3000);

    GetHAL().lvglLock();
    _is_done = true;
}

SystemUpdateWorker::~SystemUpdateWorker()
{
}

void SystemUpdateWorker::update()
{
}
