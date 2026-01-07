/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <cstdint>
#include <vector>

namespace stackchan::animation {

/**
 * @brief
 *
 */
struct FeatureKeyframe {
    uitk::Vector2i position;
    int rotation;
    int weight;

    FeatureKeyframe(int x = 0, int y = 0, int rotation = 0, int weight = 0)
        : position(x, y), rotation(rotation), weight(weight)
    {
    }
};

/**
 * @brief
 *
 */
struct ServoKeyframe {
    int angle;
    int speed;

    ServoKeyframe(int angle = 0, int speed = 0) : angle(angle), speed(speed)
    {
    }
};

/**
 * @brief
 *
 */
struct Keyframe {
    FeatureKeyframe leftEye;
    FeatureKeyframe rightEye;
    FeatureKeyframe mouth;
    ServoKeyframe yawServo;
    ServoKeyframe pitchServo;
    uint32_t durationMs = 0;

    Keyframe()
    {
    }
    Keyframe(const FeatureKeyframe& leftEye, const FeatureKeyframe& rightEye, const FeatureKeyframe& mouth,
             const ServoKeyframe& yawServo, const ServoKeyframe& pitchServo, uint32_t durationMs = 0)
        : leftEye(leftEye),
          rightEye(rightEye),
          mouth(mouth),
          yawServo(yawServo),
          pitchServo(pitchServo),
          durationMs(durationMs)
    {
    }

    void apply();

    inline uint32_t getDelayMs() const
    {
        return durationMs;
    }
};

using KeyframeSequence = std::vector<Keyframe>;

/**
 * @brief
 *
 */
class Timeline {
public:
    enum class Status {
        Idle,
        Playing,
        Paused,
        Finished,
    };

    Timeline(KeyframeSequence keyframeSequence, bool loop = false)
        : _keyframe_sequence(std::move(keyframeSequence)), _loop(loop)
    {
    }

    void start();
    void stop();
    void pause();
    void resume();
    void update();

    Status getStatus() const
    {
        return _status;
    }

    bool isFinished() const
    {
        return _status == Status::Finished;
    }

    void setLoop(bool loop)
    {
        _loop = loop;
    }

private:
    void _apply_current_keyframe();

    KeyframeSequence _keyframe_sequence;
    bool _loop;

    size_t _current_index  = 0;
    Status _status         = Status::Idle;
    uint32_t _start_time   = 0;
    uint32_t _elapsed_time = 0;
};

}  // namespace stackchan::animation
