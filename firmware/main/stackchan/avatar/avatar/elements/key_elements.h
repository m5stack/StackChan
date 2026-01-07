/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "element.h"
#include "feature.h"
#include "speech_bubble.h"
#include <functional>
#include <memory>

namespace stackchan::avatar {

/**
 * @brief Key elements of avatar
 *
 */
struct KeyElements_t {
    std::unique_ptr<Feature> leftEye;
    std::unique_ptr<Feature> rightEye;
    std::unique_ptr<Feature> mouth;
    std::unique_ptr<SpeechBubble> speechBubble;

    void forEach(std::function<void(Element*)> callback)
    {
        if (leftEye) {
            callback(leftEye.get());
        }
        if (rightEye) {
            callback(rightEye.get());
        }
        if (mouth) {
            callback(mouth.get());
        }
        if (speechBubble) {
            callback(speechBubble.get());
        }
    }
};

}  // namespace stackchan::avatar
