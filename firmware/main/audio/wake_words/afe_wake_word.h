#pragma once

#include "audio_codec.h"
#include "wake_word.h"
#include "wake_word_history_encoder.h"

#include <esp_afe_sr_models.h>
#include <model_path.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class AfeWakeWord final : public WakeWord {
public:
    AfeWakeWord();
    ~AfeWakeWord() override;

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list) override;
    void Feed(const std::vector<int16_t>& data) override;
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) override;
    void Start() override;
    void Stop() override;
    size_t GetFeedSize() override;
    void EncodeWakeWordData() override;
    bool GetWakeWordOpus(std::vector<uint8_t>& opus) override;
    const std::string& GetLastDetectedWakeWord() const override { return last_detected_wake_word_; }

private:
    static void DetectionTaskEntry(void* arg);

    bool SelectWakeModel();
    bool CreateAfe();
    std::string BuildInputFormat() const;
    void DetectionLoop();
    void HandleDetection(int model_index);

    AudioCodec* codec_ = nullptr;
    srmodel_list_t* models_ = nullptr;
    bool owns_models_ = false;

    const esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    const char* wake_model_ = nullptr;
    std::vector<std::string> wake_words_;

    TaskHandle_t detection_task_ = nullptr;
    std::atomic_bool running_{false};
    std::atomic_bool stop_task_{false};

    std::vector<int16_t> input_buffer_;
    std::mutex input_mutex_;

    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    std::string last_detected_wake_word_;
    WakeWordHistoryEncoder history_encoder_;
};
