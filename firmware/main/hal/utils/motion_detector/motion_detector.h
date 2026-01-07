/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class MotionDetector {
public:
    MotionDetector() = default;

    void update(const float& acc_x, const float& acc_y, const float& acc_z)
    {
        uint32_t now  = pdTICKS_TO_MS(xTaskGetTickCount());
        float acc_mag = std::sqrt(std::pow(acc_x, 2) + std::pow(acc_y, 2) + std::pow(acc_z, 2));

        // --- Shake Detection ---
        // Threshold ~1.5G (14.7 m/s^2)
        if (std::abs(acc_mag - 9.80665f) > 8.0f) {
            if (now - _last_shake_peak_time > 200) {       // Debounce 200ms
                if (now - _last_shake_peak_time < 1000) {  // Window 1s
                    _shake_count++;
                } else {
                    _shake_count = 1;  // Reset sequence
                }
                _last_shake_peak_time = now;

                if (_shake_count >= 3) {
                    _shake_detected = true;
                    _shake_count    = 0;
                }
            }
        }

        // --- Pick Up Detection ---
        // Check for stability first
        float diff = std::abs(acc_x - _prev_acc_x) + std::abs(acc_y - _prev_acc_y) + std::abs(acc_z - _prev_acc_z);

        _prev_acc_x = acc_x;
        _prev_acc_y = acc_y;
        _prev_acc_z = acc_z;

        // Threshold for stability (low noise)
        if (diff < 1.5f) {
            if (!_is_stable) {
                _stable_since = now;
                _is_stable    = true;
            }
        } else {
            // If it was stable for > 1s and now moving -> Pick Up
            if (_is_stable && (now - _stable_since > 1000)) {
                _pickup_detected = true;
            }
            _is_stable = false;
        }
    }

    bool isShakeDetected()
    {
        if (_shake_detected) {
            _shake_detected = false;
            return true;
        }
        return false;
    }

    bool isPickUpDetected()
    {
        if (_pickup_detected) {
            _pickup_detected = false;
            return true;
        }
        return false;
    }

private:
    int _shake_count               = 0;
    uint32_t _last_shake_peak_time = 0;
    bool _shake_detected           = false;

    bool _pickup_detected  = false;
    bool _is_stable        = false;
    uint32_t _stable_since = 0;
    float _prev_acc_x      = 0;
    float _prev_acc_y      = 0;
    float _prev_acc_z      = 0;
};
