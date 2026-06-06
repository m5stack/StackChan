#include "backlight.h"

#include <algorithm>

#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>

#include "settings.h"

namespace {

constexpr char kTag[] = "Backlight";
constexpr char kSettingsNamespace[] = "display";
constexpr char kBrightnessKey[] = "brightness";
constexpr int kTransitionIntervalUs = 5 * 1000;
constexpr int kDefaultBrightness = 75;
constexpr int kMinimumStartupBrightness = 10;
constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_0;
constexpr uint32_t kLedcDutyRange = (1u << 10) - 1;

uint8_t ClampBrightness(uint8_t brightness)
{
    return std::min<uint8_t>(brightness, 100);
}

uint8_t NormalizeStartupBrightness(int persisted_brightness)
{
    if (persisted_brightness <= 0) {
        return kMinimumStartupBrightness;
    }
    return ClampBrightness(static_cast<uint8_t>(persisted_brightness));
}

}  // namespace

Backlight::Backlight()
{
    const esp_timer_create_args_t timer_args = {
        .callback =
            [](void* context) {
                static_cast<Backlight*>(context)->OnTransitionTimer();
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "backlight_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &transition_timer_));
}

Backlight::~Backlight()
{
    if (transition_timer_ == nullptr) {
        return;
    }
    StopTransition();
    ESP_ERROR_CHECK(esp_timer_delete(transition_timer_));
}

void Backlight::RestoreBrightness()
{
    Settings settings(kSettingsNamespace);
    const int persisted_brightness = settings.GetInt(kBrightnessKey, kDefaultBrightness);
    const uint8_t brightness = NormalizeStartupBrightness(persisted_brightness);
    SetBrightness(brightness);
}

void Backlight::SetBrightness(uint8_t brightness, bool permanent)
{
    const uint8_t requested_brightness = ClampBrightness(brightness);
    if (permanent) {
        Settings settings(kSettingsNamespace, true);
        settings.SetInt(kBrightnessKey, requested_brightness);
    }

    if (brightness_ == requested_brightness && target_brightness_ == requested_brightness) {
        return;
    }

    target_brightness_ = requested_brightness;
    if (brightness_ == target_brightness_) {
        StopTransition();
        SetBrightnessImpl(brightness_);
        return;
    }

    step_ = target_brightness_ > brightness_ ? 1 : -1;
    StartTransition();
    ESP_LOGI(kTag, "Brightness target set to %u", static_cast<unsigned>(target_brightness_));
}

void Backlight::StartTransition()
{
    if (TransitionActive()) {
        return;
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(transition_timer_, kTransitionIntervalUs));
}

void Backlight::StopTransition()
{
    if (!TransitionActive()) {
        return;
    }
    ESP_ERROR_CHECK(esp_timer_stop(transition_timer_));
}

bool Backlight::TransitionActive() const
{
    return transition_timer_ != nullptr && esp_timer_is_active(transition_timer_);
}

void Backlight::OnTransitionTimer()
{
    if (brightness_ == target_brightness_) {
        StopTransition();
        return;
    }

    const int next_brightness = static_cast<int>(brightness_) + step_;
    brightness_ = ClampBrightness(static_cast<uint8_t>(std::clamp(next_brightness, 0, 100)));
    SetBrightnessImpl(brightness_);

    if (brightness_ == target_brightness_) {
        StopTransition();
    }
}

PwmBacklight::PwmBacklight(gpio_num_t pin, bool output_invert, uint32_t freq_hz)
    : Backlight()
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = kLedcMode,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = kLedcTimer,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    const ledc_channel_config_t channel_config = {
        .gpio_num = pin,
        .speed_mode = kLedcMode,
        .channel = kLedcChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = kLedcTimer,
        .duty = 0,
        .hpoint = 0,
        .flags =
            {
                .output_invert = output_invert,
            },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

PwmBacklight::~PwmBacklight()
{
    ledc_stop(kLedcMode, kLedcChannel, 0);
}

void PwmBacklight::SetBrightnessImpl(uint8_t brightness)
{
    const uint32_t duty = (kLedcDutyRange * brightness) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(kLedcMode, kLedcChannel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(kLedcMode, kLedcChannel));
}
