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

    /**
     * @brief Set the shake detection threshold.
     * @param threshold Higher value needs stronger shake. Default is 4.0f.
     *                  - 2.0f: Very sensitive (light shake)
     *                  - 4.0f: Normal
     *                  - 8.0f: Hard shake required
     */
    void setShakeThreshold(float threshold)
    {
        _shake_threshold = threshold;
    }

    void update(const float& acc_x, const float& acc_y, const float& acc_z)
    {
        uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());

        // Use differential (change in acceleration) for shake detection.
        // This effectively acts as a high-pass filter and is independent of gravity orientation.
        float diff = std::abs(acc_x - _prev_acc_x) + std::abs(acc_y - _prev_acc_y) + std::abs(acc_z - _prev_acc_z);

        _prev_acc_x = acc_x;
        _prev_acc_y = acc_y;
        _prev_acc_z = acc_z;

        // --- Shake Detection ---
        // printf("%.2f\n", diff);
        if (diff > _shake_threshold) {
            if (now - _last_shake_peak_time > 100) {       // Debounce 100ms
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
    }

    bool isShakeDetected()
    {
        if (_shake_detected) {
            _shake_detected = false;
            return true;
        }
        return false;
    }

    // bool isPickUpDetected()
    // {
    //     if (_pickup_detected) {
    //         _pickup_detected = false;
    //         return true;
    //     }
    //     return false;
    // }

private:
    int _shake_count               = 0;
    uint32_t _last_shake_peak_time = 0;
    bool _shake_detected           = false;
    float _shake_threshold         = 4.0f;

    float _prev_acc_x = 0;
    float _prev_acc_y = 0;
    float _prev_acc_z = 0;
};
