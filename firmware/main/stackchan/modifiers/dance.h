/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include "../animation/animation.h"

namespace stackchan {

class DanceModifier : public Modifier {
public:
    /**
     * @brief Happy dance
     * - Swaying left and right
     * - Eyes squinting (Happy expression)
     * - Mouth open
     */
    inline static const animation::KeyframeSequence Happy = {
        // Center, prepare
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 200}, {0, 200}, 500},
        // Sway Left, Eyes Happy (Weight 50), Mouth Open (Weight 50)
        {{-10, 0, 0, 50}, {-10, 0, 0, 50}, {0, 0, 0, 50}, {300, 200}, {-100, 200}, 800},
        // Sway Right
        {{10, 0, 0, 50}, {10, 0, 0, 50}, {0, 0, 0, 50}, {-300, 200}, {-100, 200}, 800},
        // Sway Left
        {{-10, 0, 0, 50}, {-10, 0, 0, 50}, {0, 0, 0, 50}, {300, 200}, {-100, 200}, 800},
        // Sway Right
        {{10, 0, 0, 50}, {10, 0, 0, 50}, {0, 0, 0, 50}, {-300, 200}, {-100, 200}, 800},
        // Center, Back to normal
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 200}, {0, 200}, 500},
    };

    /**
     * @brief Robot dance
     * - Stiff, jerky movements
     * - Sharp angles
     * - Eyes fixed open
     */
    inline static const animation::KeyframeSequence Robot = {
        // Center
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 500}, {0, 500}, 500},
        // Turn Left Sharp
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {450, 800}, {0, 800}, 400},
        // Pitch Down Sharp
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {450, 800}, {200, 800}, 400},
        // Turn Right Sharp (keeping pitch)
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {-450, 800}, {200, 800}, 600},
        // Pitch Up
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {-450, 800}, {0, 800}, 400},
        // Center
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 800}, {0, 800}, 400},
    };

    /**
     * @brief Panic dance
     * - Fast shaking
     * - Eyes wide open (surprised)
     * - Mouth wide open
     */
    inline static const animation::KeyframeSequence Panic = {
        // Start Panic
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 100}, {0, 1000}, {0, 1000}, 100},
        // Shake 1
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 100}, {200, 1000}, {100, 1000}, 100},
        // Shake 2
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 100}, {-200, 1000}, {-100, 1000}, 100},
        // Shake 3
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 100}, {200, 1000}, {100, 1000}, 100},
        // Shake 4
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 100}, {-200, 1000}, {-100, 1000}, 100},
        // Shake 5
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 100}, {200, 1000}, {100, 1000}, 100},
        // Calm down
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 200}, {0, 200}, 500},
    };

    /**
     * @brief Look Around
     * - Slow scanning of the environment
     */
    inline static const animation::KeyframeSequence LookAround = {
        // Center
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 200}, {0, 200}, 1000},
        // Look Left
        {{-20, 0, 0, 100}, {-20, 0, 0, 100}, {0, 0, 0, 20}, {600, 150}, {0, 150}, 2000},
        // Look Right
        {{20, 0, 0, 100}, {20, 0, 0, 100}, {0, 0, 0, 20}, {-600, 150}, {0, 150}, 2000},
        // Look Up
        {{0, -20, 0, 100}, {0, -20, 0, 100}, {0, 0, 0, 40}, {0, 150}, {-300, 150}, 1500},
        // Center
        {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 200}, {0, 200}, 1000},
    };

    DanceModifier(const animation::KeyframeSequence& sequence) : _timeline(sequence, false)
    {
        _timeline.start();
    }

    void _update(Modifiable& stackchan) override
    {
        _timeline.update();
        if (_timeline.isFinished()) {
            requestDestroy();
        }
    }

private:
    animation::Timeline _timeline;
};

}  // namespace stackchan
