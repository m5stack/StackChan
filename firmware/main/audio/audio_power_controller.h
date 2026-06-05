#pragma once

#include <atomic>
#include <cstdint>

#include <esp_timer.h>

class AudioCodec;

class AudioPowerController {
public:
    AudioPowerController() = default;
    ~AudioPowerController();

    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();

    void EnsureInputActive();
    void EnsureOutputActive();
    void TouchInput();
    void TouchOutput();

private:
    static constexpr int64_t kPowerTimeoutUs = 15LL * 1000 * 1000;
    static constexpr int64_t kPowerCheckIntervalUs = 1LL * 1000 * 1000;

    AudioCodec* codec_ = nullptr;
    esp_timer_handle_t timer_ = nullptr;
    std::atomic_bool timer_running_ = false;
    std::atomic<int64_t> last_input_time_us_ = 0;
    std::atomic<int64_t> last_output_time_us_ = 0;

    void EnsureTimerRunning();
    void StopTimer();
    void CheckAndUpdatePowerState();
};
