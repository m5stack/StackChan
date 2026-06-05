#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <model_path.h>

class AudioCodec;

#include "wake_word.h"

class WakeWordController {
public:
    void SetModelsList(srmodel_list_t* models_list);
    void SetWakeWordDetectedCallback(std::function<void(const std::string& wake_word)> callback);

    bool Initialize(AudioCodec* codec);
    void Start();
    void Stop();
    void Feed(const std::vector<int16_t>& data);

    bool HasEngine() const;
    bool IsInitialized() const;
    bool IsRunning() const;
    bool SupportsConcurrentVoiceSessionDetection() const;

    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus) const;
    const std::string& GetLastDetectedWakeWord() const;

private:
    srmodel_list_t* models_list_ = nullptr;
    std::unique_ptr<WakeWord> wake_word_;
    std::function<void(const std::string& wake_word)> on_wake_word_detected_;
    bool initialized_ = false;
    std::atomic_bool running_ = false;

    void RebuildEngine();
    void BindCallbacks();
};
