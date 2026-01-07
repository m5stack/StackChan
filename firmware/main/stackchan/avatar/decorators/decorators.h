/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../../utils/timer.h"
#include "../avatar/decorator.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <cstdint>
#include <memory>

namespace stackchan::avatar {

/**
 * @brief Decorator with a timer
 *
 */
class TimerDecorator : public Decorator {
public:
    void scheduleDestroy(uint32_t ms)
    {
        _timer.addTask(ms, 1, 0, [this]() { requestDestroy(); });
    }

    Timer& getTimer()
    {
        return _timer;
    }

    virtual void _update() override
    {
        _timer.update();
    }

private:
    Timer _timer;
};

/**
 * @brief
 *
 */
class HeartDecorator : public TimerDecorator {
public:
    /**
     * @brief
     *
     * @param parent
     * @param destroyAfterMs Destroy after milliseconds, 0 for infinite
     * @param animationIntervalMs Animation update interval in milliseconds, 0 for none
     */
    HeartDecorator(lv_obj_t* parent, uint32_t destroyAfterMs = 0, uint32_t animationIntervalMs = 500);
    ~HeartDecorator();

    using Element::setPosition;

    void setPosition(int x, int y);
    void setRotation(int rotation);
    void setColor(lv_color_t color);

private:
    std::unique_ptr<uitk::lvgl_cpp::Image> _heart;
    int _animation_index = 0;
};

/**
 * @brief
 *
 */
class AngryDecorator : public TimerDecorator {
public:
    /**
     * @brief
     *
     * @param parent
     * @param destroyAfterMs Destroy after milliseconds, 0 for infinite
     * @param animationIntervalMs Animation update interval in milliseconds, 0 for none
     */
    AngryDecorator(lv_obj_t* parent, uint32_t destroyAfterMs = 0, uint32_t animationIntervalMs = 500);
    ~AngryDecorator();

    using Element::setPosition;

    void setPosition(int x, int y);
    void setRotation(int rotation);
    void setColor(lv_color_t color);

private:
    std::unique_ptr<uitk::lvgl_cpp::Image> _angry;
    int _animation_index = 0;
};

/**
 * @brief
 *
 */
class SweatDecorator : public TimerDecorator {
public:
    /**
     * @brief
     *
     * @param parent
     * @param destroyAfterMs Destroy after milliseconds, 0 for infinite
     * @param animationIntervalMs Animation update interval in milliseconds, 0 for none
     */
    SweatDecorator(lv_obj_t* parent, uint32_t destroyAfterMs = 0, uint32_t animationIntervalMs = 700);
    ~SweatDecorator();

    using Element::setPosition;

    void setPosition(int x, int y);
    void setRotation(int rotation) override;
    void setColor(lv_color_t color);
    void setVisible(bool visible) override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Image> _sweat;
    int _animation_index = 0;
};

}  // namespace stackchan::avatar
