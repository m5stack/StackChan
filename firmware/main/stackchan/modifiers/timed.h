/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <string_view>
#include <functional>
#include <hal/hal.h>
#include <cstdint>
#include <memory>

namespace stackchan {

/**
 * @brief A timed event modifier base, which will be destroyed after the given duration
 *
 */
class TimedEventModifier : public Modifier {
public:
    TimedEventModifier(uint32_t durationMs) : _duration_ms(durationMs), _start_time(0), _is_started(false)
    {
    }

    void _update(Modifiable& stackchan) override
    {
        uint32_t now = GetHAL().millis();

        if (!_is_started) {
            _is_started = true;
            _start_time = now;
            _on_start(stackchan);

            if (_duration_ms == 0) {
                _on_end(stackchan);
                requestDestroy();
            }
            return;
        }

        if (now - _start_time >= _duration_ms) {
            _on_end(stackchan);
            requestDestroy();
        }
    }

    virtual void _on_start(Modifiable& stackchan)
    {
    }

    virtual void _on_end(Modifiable& stackchan)
    {
    }

private:
    uint32_t _duration_ms;
    uint32_t _start_time;
    bool _is_started;
};

/**
 * @brief Set emotion for the given duration
 *
 */
class TimedEmotionModifier : public TimedEventModifier {
public:
    TimedEmotionModifier(avatar::Emotion emotion, uint32_t durationMs) : TimedEventModifier(durationMs)
    {
        _target_emotion = emotion;
    }

    void _on_start(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        _prev_emotion = stackchan.avatar().getEmotion();
        stackchan.avatar().setEmotion(_target_emotion);
    }

    void _on_end(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        stackchan.avatar().setEmotion(_prev_emotion);
    }

private:
    avatar::Emotion _prev_emotion   = avatar::Emotion::Neutral;
    avatar::Emotion _target_emotion = avatar::Emotion::Neutral;
};

/**
 * @brief Set speech for the given duration
 *
 */
class TimedSpeechModifier : public TimedEventModifier {
public:
    TimedSpeechModifier(std::string_view speech, uint32_t durationMs) : TimedEventModifier(durationMs)
    {
        _target_speech = speech;
    }

    void _on_start(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        stackchan.avatar().setSpeech(_target_speech);
    }

    void _on_end(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        stackchan.avatar().clearSpeech();
    }

private:
    std::string _target_speech;
};

}  // namespace stackchan
