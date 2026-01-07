/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../utils/timer.h"
#include "../modifiable.h"
#include <string_view>
#include <functional>
#include <cstdint>
#include <memory>

namespace stackchan {

/**
 * @brief
 *
 */
class TimerModifier : public Modifier {
public:
    void scheduleDestroy(uint32_t ms)
    {
        getTimer().addTask(ms, 1, 0, [this]() { requestDestroy(); });
    }

    Timer& getTimer()
    {
        if (!_timer) {
            _timer = std::make_unique<Timer>();
        }
        return *_timer;
    }

    virtual void _update(Modifiable& stackchan) override
    {
        if (_timer) {
            _timer->update();
        }
    }

private:
    std::unique_ptr<Timer> _timer;
};

/**
 * @brief A timed event modifier base, which will be destroyed after the given duration
 *
 */
class TimedEventModifier : public TimerModifier {
public:
    TimedEventModifier(uint32_t durationMs)
    {
        if (durationMs == 0) {
            requestDestroy();
        }

        getTimer().addTask(durationMs, -1, 0, [this]() { _destroy_flag = true; });

        _is_first_in = true;
    }

    void _update(Modifiable& stackchan) override
    {
        TimerModifier::_update(stackchan);

        if (_is_first_in) {
            _is_first_in = false;
            _on_start(stackchan);
            return;
        }

        if (_destroy_flag) {
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
    bool _is_first_in  = true;
    bool _destroy_flag = false;
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
