/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "element.h"
#include <string_view>
#include <string>

namespace stackchan::avatar {

/**
 * @brief Speech bubble base class
 *
 */
class SpeechBubble : public Element {
public:
    virtual ~SpeechBubble() = default;

    virtual void setSpeech(std::string_view text)
    {
    }

    virtual void clearSpeech()
    {
    }

    virtual void setTextFont(void* font)
    {
    }
};

}  // namespace stackchan::avatar
