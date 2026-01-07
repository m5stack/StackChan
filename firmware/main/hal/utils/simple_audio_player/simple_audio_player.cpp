/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "simple_audio_player.h"
#include <opus_decoder.h>
#include <opus_resampler.h>
#include <esp_log.h>
#include <cstring>
#include "board.h"
#include "audio_codec.h"

static const char* TAG = "SimpleAudioPlayer";

#define OPUS_FRAME_DURATION_MS 60

SimpleAudioPlayer::SimpleAudioPlayer()
{
    codec_ = Board::GetInstance().GetAudioCodec();
    codec_->Start();

    // 初始化解码器和重采样器，默认参数，后续会根据 OGG 头调整
    opus_decoder_     = std::make_unique<OpusDecoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    output_resampler_ = std::make_unique<OpusResampler>();
}

SimpleAudioPlayer::~SimpleAudioPlayer()
{
    Stop();
}

struct PlayerParams {
    SimpleAudioPlayer* player;
    const uint8_t* data;
    size_t size;
    bool loop;
};

bool SimpleAudioPlayer::Play(const uint8_t* data, size_t size, bool loop)
{
    Stop();  // 停止之前的播放

    is_playing_     = true;
    stop_requested_ = false;

    PlayerParams* params = new PlayerParams{this, data, size, loop};

    BaseType_t ret = xTaskCreate(
        [](void* arg) {
            PlayerParams* p = (PlayerParams*)arg;
            p->player->PlaybackTask(p->data, p->size, p->loop);
            delete p;
            vTaskDelete(NULL);
        },
        "simple_player", 4096 * 2, params, 5, &task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create playback task");
        delete params;
        is_playing_ = false;
        return false;
    }
    return true;
}

void SimpleAudioPlayer::Stop()
{
    stop_requested_ = true;
    if (task_handle_) {
        int timeout = 100;
        while (is_playing_ && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

bool SimpleAudioPlayer::IsPlaying() const
{
    return is_playing_;
}

void SimpleAudioPlayer::PlaybackTask(const uint8_t* data, size_t size, bool loop)
{
    if (!codec_->output_enabled()) {
        codec_->EnableOutput(true);
    }

    const uint8_t* buf = data;

    auto find_page = [&](size_t start) -> size_t {
        for (size_t i = start; i + 4 <= size; ++i) {
            if (buf[i] == 'O' && buf[i + 1] == 'g' && buf[i + 2] == 'g' && buf[i + 3] == 'S') return i;
        }
        return static_cast<size_t>(-1);
    };

    do {
        size_t offset   = 0;
        bool seen_head  = false;
        bool seen_tags  = false;
        int sample_rate = 16000;  // 默认值

        // 如果是循环播放，重置解码器状态以避免爆音
        if (loop) {
            opus_decoder_->ResetState();
        }

        while (!stop_requested_) {
            // 确保输出已启用（防止被 AudioService 的自动省电逻辑关闭）
            if (!codec_->output_enabled()) {
                codec_->EnableOutput(true);
            }

            size_t pos = find_page(offset);
            if (pos == static_cast<size_t>(-1)) break;
            offset = pos;
            if (offset + 27 > size) break;

            const uint8_t* page   = buf + offset;
            uint8_t page_segments = page[26];
            size_t seg_table_off  = offset + 27;
            if (seg_table_off + page_segments > size) break;

            size_t body_size = 0;
            for (size_t i = 0; i < page_segments; ++i) body_size += page[27 + i];

            size_t body_off = seg_table_off + page_segments;
            if (body_off + body_size > size) break;

            // Parse packets using lacing
            size_t cur     = body_off;
            size_t seg_idx = 0;
            while (seg_idx < page_segments && !stop_requested_) {
                size_t pkt_len   = 0;
                size_t pkt_start = cur;
                bool continued   = false;
                do {
                    uint8_t l = page[27 + seg_idx++];
                    pkt_len += l;
                    cur += l;
                    continued = (l == 255);
                } while (continued && seg_idx < page_segments);

                if (pkt_len == 0) continue;
                const uint8_t* pkt_ptr = buf + pkt_start;

                if (!seen_head) {
                    if (pkt_len >= 19 && std::memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                        seen_head = true;
                        if (pkt_len >= 12) {
                            // uint8_t version = pkt_ptr[8];
                            // uint8_t channel_count = pkt_ptr[9];
                            if (pkt_len >= 16) {
                                sample_rate =
                                    pkt_ptr[12] | (pkt_ptr[13] << 8) | (pkt_ptr[14] << 16) | (pkt_ptr[15] << 24);
                            }
                        }
                    }
                    continue;
                }
                if (!seen_tags) {
                    if (pkt_len >= 8 && std::memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                        seen_tags = true;
                    }
                    continue;
                }

                // Audio packet (Opus)
                std::vector<uint8_t> payload(pkt_ptr, pkt_ptr + pkt_len);
                if (!DecodeAndPlay(payload, sample_rate)) {
                    ESP_LOGE(TAG, "Failed to decode and play packet");
                }
            }

            offset = body_off + body_size;
        }
    } while (loop && !stop_requested_);

    // 播放结束，不关闭 output，以免影响其他音频
    is_playing_  = false;
    task_handle_ = nullptr;
}

bool SimpleAudioPlayer::DecodeAndPlay(std::vector<uint8_t>& opus_payload, int sample_rate)
{
    // 检查采样率是否变化
    if (opus_decoder_->sample_rate() != sample_rate) {
        opus_decoder_.reset();
        opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, OPUS_FRAME_DURATION_MS);

        if (opus_decoder_->sample_rate() != codec_->output_sample_rate()) {
            output_resampler_->Configure(opus_decoder_->sample_rate(), codec_->output_sample_rate());
        }
    }

    std::vector<int16_t> pcm;
    if (opus_decoder_->Decode(std::move(opus_payload), pcm)) {
        // Resample if needed
        if (opus_decoder_->sample_rate() != codec_->output_sample_rate()) {
            int target_size = output_resampler_->GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_->Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }

        codec_->OutputData(pcm);
        return true;
    }
    return false;
}
