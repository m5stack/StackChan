#include "audio_codec.h"

#include <esp_log.h>

#include "settings.h"

namespace {

constexpr char kTag[] = "AudioCodec";
constexpr int kMinimumRestoredVolume = 10;

}  // namespace

void AudioCodec::Start()
{
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    if (output_volume_ <= 0) {
        ESP_LOGW(kTag, "Output volume %d is invalid, restoring to %d", output_volume_, kMinimumRestoredVolume);
        output_volume_ = kMinimumRestoredVolume;
    }

    ESP_LOGI(kTag, "Audio codec started");
}

void AudioCodec::SetOutputVolume(int volume)
{
    output_volume_ = volume;
    ESP_LOGI(kTag, "Set output volume to %d", output_volume_);

    Settings settings("audio", true);
    settings.SetInt("output_volume", output_volume_);
}

void AudioCodec::SetInputGain(float gain)
{
    input_gain_ = gain;
    ESP_LOGI(kTag, "Set input gain to %.1f", input_gain_);
}

void AudioCodec::EnableInput(bool enable)
{
    if (input_enabled_ == enable) {
        return;
    }

    input_enabled_ = enable;
    ESP_LOGI(kTag, "Set input enable to %s", enable ? "true" : "false");
}

void AudioCodec::EnableOutput(bool enable)
{
    if (output_enabled_ == enable) {
        return;
    }

    output_enabled_ = enable;
    ESP_LOGI(kTag, "Set output enable to %s", enable ? "true" : "false");
}

void AudioCodec::OutputData(std::vector<int16_t>& data)
{
    Write(data.data(), data.size());
}

bool AudioCodec::InputData(std::vector<int16_t>& data)
{
    return Read(data.data(), data.size()) > 0;
}
