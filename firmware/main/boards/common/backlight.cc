#include "backlight.h"

#include <algorithm>

#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>

#include "settings.h"

namespace {

constexpr char kTag[]                  = "Backlight";
constexpr int kTransitionIntervalUs    = 5 * 1000;
constexpr int kDefaultBrightness       = 75;
constexpr int kMinimumRestoredBrightness = 10;
constexpr ledc_mode_t kLedcMode        = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedcTimer      = LEDC_TIMER_0;
constexpr ledc_channel_t kLedcChannel  = LEDC_CHANNEL_0;
constexpr uint32_t kLedcMaxDuty        = (1u << 10) - 1;

}  // namespace

Backlight::Backlight()
{
    const esp_timer_create_args_t timer_args = {
        .callback =
            [](void* arg) {
                static_cast<Backlight*>(arg)->OnTransitionTimer();
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
    esp_timer_stop(transition_timer_);
    esp_timer_delete(transition_timer_);
}

void Backlight::RestoreBrightness()
{
    Settings settings("display");
    int saved_brightness = settings.GetInt("brightness", kDefaultBrightness);
    if (saved_brightness <= 0) {
        ESP_LOGW(kTag, "Brightness value (%d) is too small, restoring default minimum (%d)",
                 saved_brightness, kMinimumRestoredBrightness);
        saved_brightness = kMinimumRestoredBrightness;
    }
    SetBrightness(static_cast<uint8_t>(std::clamp(saved_brightness, 0, 100)));
}

void Backlight::SetBrightness(uint8_t brightness, bool permanent)
{
    brightness = std::min<uint8_t>(brightness, 100);

    if (permanent) {
        Settings settings("display", true);
        settings.SetInt("brightness", brightness);
    }

    if (target_brightness_ == brightness && brightness_ == brightness) {
        return;
    }

    target_brightness_ = brightness;
    step_ = (target_brightness_ >= brightness_) ? 1 : -1;

    if (brightness_ == target_brightness_) {
        SetBrightnessImpl(brightness_);
        return;
    }

    esp_err_t err = esp_timer_start_periodic(transition_timer_, kTransitionIntervalUs);
    if (err == ESP_ERR_INVALID_STATE) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(kTag, "Set brightness to %u", static_cast<unsigned>(brightness));
}

void Backlight::OnTransitionTimer()
{
    if (brightness_ == target_brightness_) {
        esp_timer_stop(transition_timer_);
        return;
    }

    const int next_brightness = static_cast<int>(brightness_) + step_;
    brightness_ = static_cast<uint8_t>(std::clamp(next_brightness, 0, 100));
    SetBrightnessImpl(brightness_);

    if (brightness_ == target_brightness_) {
        esp_timer_stop(transition_timer_);
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
    const uint32_t duty_cycle = (kLedcMaxDuty * brightness) / 100;
    ledc_set_duty(kLedcMode, kLedcChannel, duty_cycle);
    ledc_update_duty(kLedcMode, kLedcChannel);
}
