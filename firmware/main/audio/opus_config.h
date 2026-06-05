#pragma once

#include <esp_audio_enc.h>
#include <esp_audio_types.h>
#include <esp_opus_enc.h>

namespace audio::opus {

inline esp_opus_enc_frame_duration_t GetFrameDurationEnum(int duration_ms)
{
    switch (duration_ms) {
        case 5:
            return ESP_OPUS_ENC_FRAME_DURATION_5_MS;
        case 10:
            return ESP_OPUS_ENC_FRAME_DURATION_10_MS;
        case 20:
            return ESP_OPUS_ENC_FRAME_DURATION_20_MS;
        case 40:
            return ESP_OPUS_ENC_FRAME_DURATION_40_MS;
        case 60:
            return ESP_OPUS_ENC_FRAME_DURATION_60_MS;
        case 80:
            return ESP_OPUS_ENC_FRAME_DURATION_80_MS;
        case 100:
            return ESP_OPUS_ENC_FRAME_DURATION_100_MS;
        case 120:
            return ESP_OPUS_ENC_FRAME_DURATION_120_MS;
        default:
            return static_cast<esp_opus_enc_frame_duration_t>(-1);
    }
}

inline esp_opus_enc_config_t MakeEncoderConfig(int frame_duration_ms)
{
    return {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = GetFrameDurationEnum(frame_duration_ms),
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };
}

}  // namespace audio::opus
