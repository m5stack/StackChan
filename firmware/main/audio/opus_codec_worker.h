#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <esp_ae_rate_cvt.h>
#include <esp_audio_enc.h>
#include <esp_audio_types.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>

#include "audio_types.h"
#include "task_worker.h"

class AudioBus;
class AudioCodec;
struct AudioStreamPacket;

class OpusCodecWorker final : public TaskWorker {
public:
    OpusCodecWorker(AudioBus& bus, AudioCodec& codec);
    ~OpusCodecWorker() override;

    bool Initialize();
    bool Start();
    void Stop();
    void ResetDecoder();
    void SetSendQueueAvailableCallback(std::function<void()> callback);

private:
    static constexpr int kEncoderSampleRate = 16000;
    static constexpr int kFrameDurationMs = 60;
    static constexpr int kMaxPlaybackTasksInQueue = 2;
    static constexpr int kMaxSendPacketsInQueue = (2400 / kFrameDurationMs);

    class OpusEncoderHandle {
    public:
        ~OpusEncoderHandle();
        bool Open();
        void Close();
        bool IsOpen() const;
        bool Encode(const std::vector<int16_t>& pcm, std::vector<uint8_t>& encoded) const;
        int frame_samples() const;
        int sample_rate() const;
        int frame_duration_ms() const;

    private:
        void* handle_ = nullptr;
        int frame_samples_ = 0;
        int output_buffer_size_ = 0;
    };

    class OpusDecoderHandle {
    public:
        ~OpusDecoderHandle();
        bool Open(int sample_rate, int frame_duration_ms);
        void Close();
        void Reset();
        bool IsOpen() const;
        bool Decode(const AudioStreamPacket& packet, std::vector<int16_t>& pcm) const;
        int sample_rate() const;
        int frame_duration_ms() const;
        int frame_samples() const;

    private:
        mutable std::mutex mutex_;
        void* handle_ = nullptr;
        int sample_rate_ = 0;
        int frame_duration_ms_ = kFrameDurationMs;
        int frame_samples_ = 0;
    };

    class RateConverterHandle {
    public:
        ~RateConverterHandle();
        bool Configure(int src_rate, int dest_rate, int channels);
        void Reset();
        bool IsConfigured() const;
        void Convert(std::vector<int16_t>& pcm) const;

    private:
        mutable std::mutex mutex_;
        esp_ae_rate_cvt_handle_t handle_ = nullptr;
        int src_rate_ = 0;
        int dest_rate_ = 0;
        int channels_ = 0;
    };

    AudioBus& bus_;
    AudioCodec& codec_;
    OpusEncoderHandle encoder_;
    OpusDecoderHandle decoder_;
    RateConverterHandle output_resampler_;
    std::function<void()> on_send_queue_available_;

    void Run() override;
    void HandleDecodeWork();
    void HandleEncodeWork();
    bool EnsureDecoderConfigured(int sample_rate, int frame_duration_ms);
};
