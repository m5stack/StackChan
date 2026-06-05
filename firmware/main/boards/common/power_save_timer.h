#pragma once

#include <functional>

#include <esp_pm.h>
#include <esp_timer.h>

class PowerSaveTimer {
public:
    PowerSaveTimer(int cpu_max_freq_mhz, int seconds_to_sleep = 20, int seconds_to_shutdown = -1);
    ~PowerSaveTimer();

    void SetSleepEligibilityCallback(std::function<bool()> callback);
    void SetWakeWordRunningCallback(std::function<bool()> callback);
    void SetWakeWordDetectionCallback(std::function<void(bool)> callback);
    void SetEnabled(bool enabled);
    void OnEnterSleepMode(std::function<void()> callback);
    void OnExitSleepMode(std::function<void()> callback);
    void OnShutdownRequest(std::function<void()> callback);
    void WakeUp();

private:
    void Tick();
    bool SleepModeAllowed() const;
    void EnterSleepMode();
    void ExitSleepMode();

    esp_timer_handle_t power_save_timer_ = nullptr;
    bool enabled_ = false;
    bool in_sleep_mode_ = false;
    bool wake_word_was_running_ = false;
    int idle_ticks_ = 0;
    int cpu_max_freq_mhz_;
    int seconds_to_sleep_;
    int seconds_to_shutdown_;

    std::function<bool()> can_enter_sleep_;
    std::function<bool()> is_wake_word_running_;
    std::function<void(bool)> set_wake_word_detection_;
    std::function<void()> on_enter_sleep_mode_;
    std::function<void()> on_exit_sleep_mode_;
    std::function<void()> on_shutdown_request_;
};
