#include "audio_power_controller.h"

#include <esp_err.h>
#include <esp_log.h>

#include "audio_codec.h"

namespace {

constexpr char kTag[] = "AudioPower";

int64_t NowUs()
{
    return esp_timer_get_time();
}

}  // namespace

AudioPowerController::~AudioPowerController()
{
    Stop();
    if (timer_ != nullptr) {
        esp_timer_delete(timer_);
    }
}

void AudioPowerController::Initialize(AudioCodec* codec)
{
    codec_ = codec;
    if (timer_ != nullptr) {
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<AudioPowerController*>(arg)->CheckAndUpdatePowerState();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_power_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_));
}

void AudioPowerController::Start()
{
    const int64_t now_us = NowUs();
    last_input_time_us_ = now_us;
    last_output_time_us_ = now_us;
    EnsureTimerRunning();
}

void AudioPowerController::Stop()
{
    StopTimer();
}

void AudioPowerController::EnsureInputActive()
{
    if (codec_ == nullptr) {
        return;
    }
    if (!codec_->input_enabled()) {
        EnsureTimerRunning();
        codec_->EnableInput(true);
    }
}

void AudioPowerController::EnsureOutputActive()
{
    if (codec_ == nullptr) {
        return;
    }
    if (!codec_->output_enabled()) {
        EnsureTimerRunning();
        codec_->EnableOutput(true);
    }
}

void AudioPowerController::TouchInput()
{
    last_input_time_us_ = NowUs();
}

void AudioPowerController::TouchOutput()
{
    last_output_time_us_ = NowUs();
}

void AudioPowerController::EnsureTimerRunning()
{
    if (timer_ == nullptr) {
        return;
    }

    bool expected = false;
    if (!timer_running_.compare_exchange_strong(expected, true)) {
        return;
    }

    const esp_err_t err = esp_timer_start_periodic(timer_, kPowerCheckIntervalUs);
    if (err != ESP_OK) {
        timer_running_ = false;
        ESP_LOGE(kTag, "Failed to start power timer: %s", esp_err_to_name(err));
    }
}

void AudioPowerController::StopTimer()
{
    if (timer_ == nullptr) {
        return;
    }

    bool expected = true;
    if (!timer_running_.compare_exchange_strong(expected, false)) {
        return;
    }

    const esp_err_t err = esp_timer_stop(timer_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "Failed to stop power timer: %s", esp_err_to_name(err));
    }
}

void AudioPowerController::CheckAndUpdatePowerState()
{
    if (codec_ == nullptr) {
        StopTimer();
        return;
    }

    const int64_t now_us = NowUs();
    const int64_t input_elapsed_us = now_us - last_input_time_us_.load();
    const int64_t output_elapsed_us = now_us - last_output_time_us_.load();

    if (input_elapsed_us > kPowerTimeoutUs && codec_->input_enabled()) {
        codec_->EnableInput(false);
    }
    if (output_elapsed_us > kPowerTimeoutUs && codec_->output_enabled()) {
        if (!(codec_->duplex() && codec_->input_enabled())) {
            codec_->EnableOutput(false);
        }
    }
    if (!codec_->input_enabled() && !codec_->output_enabled()) {
        StopTimer();
    }
}
