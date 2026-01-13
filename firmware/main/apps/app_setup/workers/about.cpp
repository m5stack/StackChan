/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <stackchan/stackchan.h>
#include <games/breakout/breakout.hpp>
#include <memory>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <functional>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace uitk::games;
using namespace uitk::games::breakout;
using namespace setup_workers;

static std::string _tag = "Setup-About";

class BreakoutLvgl : public Breakout {
public:
    BreakoutLvgl()
    {
        _canvas = std::make_unique<Canvas>(lv_screen_active());
        _canvas->setSize(320, 240);
        _canvas->align(LV_ALIGN_CENTER, 0, 0);
        _canvas->setBgOpa(LV_OPA_COVER);
        _canvas->setBgColor(lv_color_white());
        _canvas->createBuffer(320, 240, LV_COLOR_FORMAT_RGB565);

        auto create_button = [](int x, int w, bool* key_state) {
            auto btn = std::make_unique<Button>(lv_screen_active());
            btn->setSize(w, 240);
            btn->align(LV_ALIGN_TOP_LEFT, x, 0);
            btn->setBgOpa(LV_OPA_TRANSP);
            btn->setBorderWidth(0);
            btn->setShadowWidth(0);

            btn->onPressed(
                [](lv_event_t* e) {
                    if (bool* s = (bool*)lv_event_get_user_data(e)) *s = true;
                },
                key_state);

            auto on_release = [](lv_event_t* e) {
                if (bool* s = (bool*)lv_event_get_user_data(e)) *s = false;
            };

            btn->onRelease(on_release, key_state);
            btn->addEventCb(on_release, LV_EVENT_PRESS_LOST, key_state);

            return btn;
        };

        _btn_left  = create_button(0, 106, &_left);
        _btn_fire  = create_button(106, 108, &_fire);
        _btn_right = create_button(214, 106, &_right);
    }

    ~BreakoutLvgl() override
    {
        _canvas->releaseBuffer();
    }

    bool gameOver = false;

protected:
    // Define a simple level
    LevelDesc level1()
    {
        LevelDesc level;

        // Walls
        level.walls = {
            {{2, 120}, {4, 240}},
            {{318, 120}, {4, 240}},
            {{160, 2}, {320, 4}},
        };

        // Paddle
        level.paddle = {{160, 220}, {60, 6}, 250, {24, 296}};

        // Ball
        level.ball = {{0, 0}, 3, 150};

        // Bricks
        const int rows    = 5;
        const int cols    = 10;
        Vector2 brickSize = {28, 10};

        float startX = 160.0f - (cols * brickSize.x) * 0.5f + brickSize.x * 0.5f;
        float startY = 30;

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                level.bricks.push_back({{startX + x * brickSize.x, startY + y * brickSize.y}, brickSize});
            }
        }

        level.screenHeight = 240.0f;
        level.lives        = 5;

        return level;
    }

    // Build the level
    void onBuildLevel() override
    {
        loadLevel(level1());
    }

    // Handle user input
    bool onReadAction(Action action) override
    {
        switch (action) {
            case Action::MoveLeft:
                return _left;
            case Action::MoveRight:
                return _right;
            case Action::Fire:
                return _fire;
        }
        return false;
    }

    // Render the game objects
    void onRender(float dt) override
    {
        _canvas->fillBg(lv_color_white());

        _canvas->startDrawing();

        getWorld().forEachObject([&](GameObject* obj) {
            auto group = static_cast<Group>(obj->groupId);

            if (group == Group::Wall || group == Group::Player || group == Group::Brick) {
                lv_color_t color;
                if (group == Group::Wall || group == Group::Player) {
                    color = lv_palette_main(LV_PALETTE_GREY);
                } else {
                    color = lv_palette_main(LV_PALETTE_ORANGE);
                }

                auto p = obj->get<Transform>()->position;
                auto s = obj->get<RectShape>()->size;

                _canvas->drawRect((int32_t)(p.x - s.x / 2.0f), (int32_t)(p.y - s.y / 2.0f), (int32_t)s.x, (int32_t)s.y,
                                  color);
            }

            else if (group == Group::Ball) {
                auto p = obj->get<Transform>()->position;
                auto r = obj->get<CircleShape>()->radius;

                _canvas->drawCircle((int32_t)p.x, (int32_t)p.y, (int32_t)r, lv_palette_main(LV_PALETTE_RED));
            }
        });

        _canvas->finishDrawing();
    }

    void onGameOver() override
    {
        gameOver = true;
    }

private:
    std::unique_ptr<Canvas> _canvas;
    std::unique_ptr<Button> _btn_left;
    std::unique_ptr<Button> _btn_right;
    std::unique_ptr<Button> _btn_fire;
    bool _left  = false;
    bool _right = false;
    bool _fire  = false;
};
static std::unique_ptr<BreakoutLvgl> _breakout;

FwVersionWorker::FwVersionWorker()
{
    _breakout = std::make_unique<BreakoutLvgl>();
    _breakout->init();
}

FwVersionWorker::~FwVersionWorker()
{
    _breakout.reset();
}

void FwVersionWorker::update()
{
    if (GetHAL().millis() - _last_tick > 33) {
        _last_tick = GetHAL().millis();
        _breakout->update();
        if (_breakout->gameOver) {
            _is_done = true;
        }
    }
}
