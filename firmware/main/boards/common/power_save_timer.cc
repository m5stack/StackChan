#include "power_save_timer.h"

#include <esp_log.h>

#include "audio_codec.h"
#include "board.h"
#include "settings.h"

namespace {

constexpr char kTag[] = "PowerSaveTimer";
constexpr int64_t kTickPeriodUs = 1000000;
constexpr int kMinCpuFreqMhz = 40;

}  // namespace

PowerSaveTimer::PowerSaveTimer(int cpu_max_freq_mhz, int seconds_to_sleep, int seconds_to_shutdown)
    : cpu_max_freq_mhz_(cpu_max_freq_mhz),
      seconds_to_sleep_(seconds_to_sleep),
      seconds_to_shutdown_(seconds_to_shutdown)
{
    const esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) { static_cast<PowerSaveTimer*>(arg)->Tick(); },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "power_save_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &power_save_timer_));
}

PowerSaveTimer::~PowerSaveTimer()
{
    if (power_save_timer_ == nullptr) {
        return;
    }
    esp_timer_stop(power_save_timer_);
    esp_timer_delete(power_save_timer_);
}

void PowerSaveTimer::SetSleepEligibilityCallback(std::function<bool()> callback)
{
    can_enter_sleep_ = std::move(callback);
}

void PowerSaveTimer::SetWakeWordRunningCallback(std::function<bool()> callback)
{
    is_wake_word_running_ = std::move(callback);
}

void PowerSaveTimer::SetWakeWordDetectionCallback(std::function<void(bool)> callback)
{
    set_wake_word_detection_ = std::move(callback);
}

void PowerSaveTimer::SetEnabled(bool enabled)
{
    if (enabled == enabled_) {
        return;
    }

    if (enabled) {
        Settings settings("wifi", false);
        if (!settings.GetBool("sleep_mode", true)) {
            ESP_LOGI(kTag, "Power save timer is disabled by settings");
            return;
        }

        idle_ticks_ = 0;
        enabled_ = true;
        ESP_ERROR_CHECK(esp_timer_start_periodic(power_save_timer_, kTickPeriodUs));
        ESP_LOGI(kTag, "Power save timer enabled");
        return;
    }

    ESP_ERROR_CHECK(esp_timer_stop(power_save_timer_));
    enabled_ = false;
    WakeUp();
    ESP_LOGI(kTag, "Power save timer disabled");
}

void PowerSaveTimer::OnEnterSleepMode(std::function<void()> callback)
{
    on_enter_sleep_mode_ = callback;
}

void PowerSaveTimer::OnExitSleepMode(std::function<void()> callback)
{
    on_exit_sleep_mode_ = callback;
}

void PowerSaveTimer::OnShutdownRequest(std::function<void()> callback)
{
    on_shutdown_request_ = callback;
}

void PowerSaveTimer::Tick()
{
    if (!in_sleep_mode_ && !SleepModeAllowed()) {
        idle_ticks_ = 0;
        return;
    }

    ++idle_ticks_;

    if (seconds_to_sleep_ != -1 && idle_ticks_ >= seconds_to_sleep_ && !in_sleep_mode_) {
        EnterSleepMode();
    }

    if (seconds_to_shutdown_ != -1 && idle_ticks_ >= seconds_to_shutdown_ && on_shutdown_request_ != nullptr) {
        on_shutdown_request_();
    }
}

bool PowerSaveTimer::SleepModeAllowed() const
{
    return can_enter_sleep_ == nullptr || can_enter_sleep_();
}

void PowerSaveTimer::EnterSleepMode()
{
    ESP_LOGI(kTag, "Enabling power save mode");
    in_sleep_mode_ = true;

    if (on_enter_sleep_mode_ != nullptr) {
        on_enter_sleep_mode_();
    }

    if (cpu_max_freq_mhz_ == -1) {
        return;
    }

    wake_word_was_running_ = is_wake_word_running_ != nullptr && is_wake_word_running_();
    if (wake_word_was_running_ && set_wake_word_detection_ != nullptr) {
        set_wake_word_detection_(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (AudioCodec* codec = Board::GetInstance().GetAudioCodec(); codec != nullptr) {
        codec->EnableInput(false);
    }

    const esp_pm_config_t pm_config = {
        .max_freq_mhz = cpu_max_freq_mhz_,
        .min_freq_mhz = kMinCpuFreqMhz,
        .light_sleep_enable = true,
    };
    esp_pm_configure(&pm_config);
}

void PowerSaveTimer::ExitSleepMode()
{
    ESP_LOGI(kTag, "Exiting power save mode");
    in_sleep_mode_ = false;

    if (cpu_max_freq_mhz_ != -1) {
        const esp_pm_config_t pm_config = {
            .max_freq_mhz = cpu_max_freq_mhz_,
            .min_freq_mhz = cpu_max_freq_mhz_,
            .light_sleep_enable = false,
        };
        esp_pm_configure(&pm_config);

        if (wake_word_was_running_ && set_wake_word_detection_ != nullptr) {
            set_wake_word_detection_(true);
        }
    }

    if (on_exit_sleep_mode_ != nullptr) {
        on_exit_sleep_mode_();
    }
}

void PowerSaveTimer::WakeUp()
{
    idle_ticks_ = 0;
    if (!in_sleep_mode_) {
        return;
    }
    ExitSleepMode();
}
