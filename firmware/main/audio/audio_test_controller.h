#pragma once

#include <atomic>

class AudioBus;

class AudioTestController {
public:
    explicit AudioTestController(AudioBus& bus);

    void Start();
    void Stop();
    void Reset();

    bool IsRunning() const;
    bool IsFull() const;

private:
    static constexpr int kMaxTestingFrames = 10000 / 60;

    AudioBus& bus_;
    std::atomic_bool running_ = false;
};
