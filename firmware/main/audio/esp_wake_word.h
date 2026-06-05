#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <esp_wn_iface.h>
#include <esp_wn_models.h>
#include <model_path.h>

#include "wake_word.h"

class EspWakeWord final : public WakeWord {
public:
    EspWakeWord() = default;
    ~EspWakeWord() override;

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list) override;
    void Feed(const std::vector<int16_t>& data) override;
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) override;
    void Start() override;
    void Stop() override;
    size_t GetFeedSize() override;
    void EncodeWakeWordData() override;
    bool GetWakeWordOpus(std::vector<uint8_t>& opus) override;
    const std::string& GetLastDetectedWakeWord() const override;

private:
    const esp_wn_iface_t* wakenet_iface_ = nullptr;
    model_iface_data_t* wakenet_data_ = nullptr;
    srmodel_list_t* wakenet_model_ = nullptr;
    AudioCodec* codec_ = nullptr;
    std::atomic<bool> running_ = false;

    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    std::string last_detected_wake_word_;
    std::vector<int16_t> input_buffer_;
    std::mutex input_buffer_mutex_;
};
