/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class AudioCodec;
class OpusDecoderWrapper;
class OpusResampler;

class SimpleAudioPlayer {
public:
    SimpleAudioPlayer();
    ~SimpleAudioPlayer();

    // 播放 OGG 音频数据
    // 注意：数据必须在播放期间保持有效（例如存储在 Flash 中的数据）
    // 不会复制数据
    // loop: 是否循环播放
    bool Play(const uint8_t* data, size_t size, bool loop = false);

    // 停止播放
    void Stop();

    // 是否正在播放
    bool IsPlaying() const;

private:
    void PlaybackTask(const uint8_t* data, size_t size, bool loop);
    bool DecodeAndPlay(std::vector<uint8_t>& opus_payload, int sample_rate);

    AudioCodec* codec_ = nullptr;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;
    std::unique_ptr<OpusResampler> output_resampler_;

    TaskHandle_t task_handle_     = nullptr;
    volatile bool is_playing_     = false;
    volatile bool stop_requested_ = false;
};
