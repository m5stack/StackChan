/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../avatar/decorator.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <cstdint>
#include <memory>

namespace stackchan::avatar {

/**
 * @brief
 *
 */
class HeartDecorator : public Decorator {
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

    void _update() override;

    using Element::setPosition;

    void setPosition(int x, int y);
    void setRotation(int rotation) override;
    void setColor(lv_color_t color);

private:
    std::unique_ptr<uitk::lvgl_cpp::Image> _heart;

    uint32_t _destroy_at            = 0;
    uint32_t _next_animation_tick   = 0;
    uint32_t _animation_interval_ms = 0;
    bool _has_lifetime              = false;

    int _animation_index = 0;
};

/**
 * @brief
 *
 */
class AngryDecorator : public Decorator {
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

    void _update() override;

    using Element::setPosition;

    void setPosition(int x, int y);
    void setRotation(int rotation) override;
    void setColor(lv_color_t color);

private:
    std::unique_ptr<uitk::lvgl_cpp::Image> _angry;

    uint32_t _destroy_at            = 0;
    uint32_t _next_animation_tick   = 0;
    uint32_t _animation_interval_ms = 0;
    bool _has_lifetime              = false;

    int _animation_index = 0;
};

/**
 * @brief
 *
 */
class SweatDecorator : public Decorator {
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

    void _update() override;

    using Element::setPosition;

    void setPosition(int x, int y);
    void setRotation(int rotation) override;
    void setColor(lv_color_t color);
    void setVisible(bool visible) override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Image> _sweat;

    uint32_t _destroy_at            = 0;
    uint32_t _next_animation_tick   = 0;
    uint32_t _animation_interval_ms = 0;
    bool _has_lifetime              = false;

    int _animation_index = 0;
};

}  // namespace stackchan::avatar
