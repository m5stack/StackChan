#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include "audio_processor.h"

class NoAudioProcessor final : public AudioProcessor {
public:
    NoAudioProcessor() = default;

    void Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) override;
    void Feed(std::vector<int16_t>&& data) override;
    void Start() override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) override;
    void OnVadStateChange(std::function<void(bool speaking)> callback) override;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;

private:
    void ResetFrameState();
    void AppendMonoSamples(const std::vector<int16_t>& data);
    void EmitCompleteFrames();

    AudioCodec* codec_ = nullptr;
    int frame_samples_ = 0;
    std::vector<int16_t> pending_samples_;
    std::function<void(std::vector<int16_t>&& data)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;
    std::atomic<bool> is_running_ = false;
};
