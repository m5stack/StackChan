#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <esp_ae_rate_cvt.h>

#include "audio_types.h"
#include "task_worker.h"

class AudioBus;
class AudioCodec;
class AudioPowerController;
class AudioTestController;
class VoiceProcessorController;
class WakeWordController;

class AudioCaptureWorker final : public TaskWorker {
public:
    AudioCaptureWorker(
        AudioCodec& codec,
        AudioPowerController& power,
        WakeWordController& wake_word,
        VoiceProcessorController& voice_processor,
        AudioTestController& test_controller,
        AudioBus& bus);
    ~AudioCaptureWorker() override;

    bool Initialize();
    bool Start();
    void Stop();

    void EnableWakeWord(bool enable);
    void EnableVoiceProcessing(bool enable);
    void RequestWarmup();
    void ResetInputResampler();

    void SetEncodePcmCallback(std::function<void(AudioFrameTarget, std::vector<int16_t>&&)> callback);
    bool ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples);

private:
    static constexpr int kTargetInputSampleRate = 16000;
    static constexpr int kTestingFrameDurationMs = 60;
    static constexpr int kRealtimeCaptureChunkSamples = 160;
    static constexpr TickType_t kWarmupDelay = pdMS_TO_TICKS(120);

    AudioCodec& codec_;
    AudioPowerController& power_;
    WakeWordController& wake_word_;
    VoiceProcessorController& voice_processor_;
    AudioTestController& test_controller_;
    AudioBus& bus_;
    std::function<void(AudioFrameTarget, std::vector<int16_t>&&)> on_encode_pcm_;
    esp_ae_rate_cvt_handle_t input_resampler_ = nullptr;
    std::mutex input_resampler_mutex_;
    std::atomic_bool wake_word_enabled_ = false;
    std::atomic_bool voice_processing_enabled_ = false;
    std::atomic_bool warmup_requested_ = false;

    void Run() override;
    bool CaptureTestingFrame();
    bool CaptureRealtimeFrame();
    void DestroyInputResampler();
};
