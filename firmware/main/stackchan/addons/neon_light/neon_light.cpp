/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "neon_light.h"
#include <hal/hal.h>

using namespace stackchan::addon;

void NeonLight::init()
{
    // Setup color animation
    _color_anim.duration = 0.3f;
    _color_anim.begin();

    _is_inited = true;
}

void NeonLight::update()
{
    if (!_is_inited) {
        init();
    }

    // Keep update in at most 50Hz
    if (GetHAL().millis() - _last_tick < 20) {
        return;
    }
    _last_tick = GetHAL().millis();

    // Apply color animation
    if (!_color_anim.done()) {
        _color_anim.updateWithDelta(0.02f);  // Fixed delta time for consistency
        for (int i = 0; i < _led_count; i++) {
            set_rgb_color_impl(i, _color_anim.r, _color_anim.g, _color_anim.b);
        }
        refresh_rgb_impl();
    }

    // Snap to target angle when animation ends
    else if (_snap_to_target_on_rest) {
        _snap_to_target_on_rest = false;
        for (int i = 0; i < _led_count; i++) {
            set_rgb_color_impl(i, _color_anim.r, _color_anim.g, _color_anim.b);
        }
        refresh_rgb_impl();
    }
}

void NeonLight::setColor(uint8_t r, uint8_t g, uint8_t b)
{
    _color_anim.move(r, g, b);
    _snap_to_target_on_rest = true;
}

void NeonLight::setColor(const uitk::color::Rgb_t& rgb)
{
    _color_anim.move(rgb);
    _snap_to_target_on_rest = true;
}

void NeonLight::setColor(uint32_t hex)
{
    _color_anim.move(hex);
    _snap_to_target_on_rest = true;
}

void NeonLight::setColor(std::string_view hex)
{
    _color_anim.move(hex);
    _snap_to_target_on_rest = true;
}

void NeonLight::setDuration(float durationSec)
{
    _color_anim.duration = durationSec;
    _color_anim.begin();
}

void LeftNeonLight::set_rgb_color_impl(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    GetHAL().setRgbColor(index, r, g, b);
}

void LeftNeonLight::refresh_rgb_impl()
{
    GetHAL().refreshRgb();
}

void RightNeonLight::set_rgb_color_impl(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    GetHAL().setRgbColor(index + 6, r, g, b);
}

void RightNeonLight::refresh_rgb_impl()
{
    GetHAL().refreshRgb();
}
