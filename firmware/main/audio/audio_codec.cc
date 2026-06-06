#include "audio_codec.h"

#include <algorithm>

#include <esp_log.h>

#include "settings.h"

namespace {

constexpr char kTag[] = "AudioCodec";
constexpr char kSettingsNamespace[] = "audio";
constexpr char kOutputVolumeKey[] = "output_volume";
constexpr int kMinimumStartupVolume = 10;
constexpr int kMaximumOutputVolume = 100;
constexpr float kMinimumInputGain = 0.0f;

int NormalizeVolumeForRuntime(int volume)
{
    return std::clamp(volume, 0, kMaximumOutputVolume);
}

int NormalizeVolumeForStartup(int volume)
{
    return volume <= 0 ? kMinimumStartupVolume : NormalizeVolumeForRuntime(volume);
}

}  // namespace

void AudioCodec::Start()
{
    Settings settings(kSettingsNamespace, false);
    const int persisted_volume = settings.GetInt(kOutputVolumeKey, output_volume_);
    output_volume_ = NormalizeVolumeForStartup(persisted_volume);
    ESP_LOGI(kTag, "Audio codec ready with output volume %d", output_volume_);
}

void AudioCodec::SetOutputVolume(int volume)
{
    const int normalized_volume = NormalizeVolumeForRuntime(volume);
    if (normalized_volume == output_volume_) {
        return;
    }

    output_volume_ = normalized_volume;
    Settings settings(kSettingsNamespace, true);
    settings.SetInt(kOutputVolumeKey, output_volume_);
    ESP_LOGI(kTag, "Output volume set to %d", output_volume_);
}

void AudioCodec::SetInputGain(float gain)
{
    const float normalized_gain = std::max(gain, kMinimumInputGain);
    if (normalized_gain == input_gain_) {
        return;
    }

    input_gain_ = normalized_gain;
    ESP_LOGI(kTag, "Input gain set to %.1f", input_gain_);
}

void AudioCodec::EnableInput(bool enable)
{
    if (input_enabled_ == enable) {
        return;
    }

    input_enabled_ = enable;
    ESP_LOGI(kTag, "Input path %s", input_enabled_ ? "enabled" : "disabled");
}

void AudioCodec::EnableOutput(bool enable)
{
    if (output_enabled_ == enable) {
        return;
    }

    output_enabled_ = enable;
    ESP_LOGI(kTag, "Output path %s", output_enabled_ ? "enabled" : "disabled");
}

void AudioCodec::OutputData(std::vector<int16_t>& data)
{
    if (data.empty()) {
        return;
    }
    Write(data.data(), static_cast<int>(data.size()));
}

bool AudioCodec::InputData(std::vector<int16_t>& data)
{
    if (data.empty()) {
        return false;
    }
    return Read(data.data(), static_cast<int>(data.size())) > 0;
}
