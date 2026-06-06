#pragma once

#include "audio_codec.h"
#include "wake_word.h"
#include "wake_word_history_encoder.h"

#include <esp_mn_iface.h>
#include <model_path.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class CustomWakeWord final : public WakeWord {
public:
    CustomWakeWord();
    ~CustomWakeWord() override;

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
    struct Command {
        int id = 0;
        std::string phrase;
        std::string display_text;
        std::string action;
    };

    bool LoadModels(srmodel_list_t* models_list);
    bool LoadAssetConfig();
    bool CreateMultinet();
    bool ConfigureCommands();
    void AppendInput(const std::vector<int16_t>& data);
    void ProcessBufferedInput();
    void HandleDetection();

    AudioCodec* codec_ = nullptr;
    srmodel_list_t* models_ = nullptr;
    bool owns_models_ = false;

    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* model_data_ = nullptr;
    char* model_name_ = nullptr;

    std::string language_ = "cn";
    int duration_ms_ = 3000;
    float threshold_ = 0.2f;
    std::vector<Command> commands_;

    std::atomic_bool running_{false};
    std::vector<int16_t> input_buffer_;
    std::mutex input_mutex_;

    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    std::string last_detected_wake_word_;
    WakeWordHistoryEncoder history_encoder_;
};
