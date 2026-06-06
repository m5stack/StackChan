#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <esp_afe_sr_models.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio_processor.h"

class AudioCodec;

class AfeAudioProcessor final : public AudioProcessor {
public:
    AfeAudioProcessor() = default;
    ~AfeAudioProcessor() override;

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
    static constexpr TickType_t kFetchTimeout = pdMS_TO_TICKS(100);

    std::string BuildInputFormat() const;
    void ConfigureModels(srmodel_list_t* models_list);
    void CreateAfe(const std::string& input_format);
    void FeedBufferedChunksLocked();
    void EmitVadChange(vad_state_t state);
    void EmitProcessedAudio(const int16_t* data, size_t samples);
    void TaskLoop();

    AudioCodec* codec_ = nullptr;
    srmodel_list_t* owned_models_ = nullptr;
    const esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    char* ns_model_name_ = nullptr;
    char* vad_model_name_ = nullptr;

    std::function<void(std::vector<int16_t>&& data)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;

    std::mutex input_mutex_;
    std::vector<int16_t> input_buffer_;
    std::vector<int16_t> output_buffer_;
    int frame_samples_ = 0;
    std::atomic_bool running_ = false;
    bool speaking_ = false;
};
