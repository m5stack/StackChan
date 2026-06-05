#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <model_path.h>

class AudioCodec;

#include "audio_processor.h"

class VoiceProcessorController {
public:
    VoiceProcessorController();

    void SetModelsList(srmodel_list_t* models_list);
    void SetOutputCallback(std::function<void(std::vector<int16_t>&& data)> callback);
    void SetVadStateCallback(std::function<void(bool speaking)> callback);

    bool Initialize(AudioCodec* codec, int frame_duration_ms);
    void Start();
    void Stop();
    void Feed(std::vector<int16_t>&& data);
    void EnableDeviceAec(bool enable);

    bool IsInitialized() const;
    bool IsRunning() const;
    bool IsVoiceDetected() const;

private:
    std::unique_ptr<AudioProcessor> processor_;
    srmodel_list_t* models_list_ = nullptr;
    int frame_duration_ms_ = 0;
    bool initialized_ = false;
    std::atomic_bool voice_detected_ = false;
    std::function<void(std::vector<int16_t>&& data)> on_output_;
    std::function<void(bool speaking)> on_vad_state_change_;

    void BindCallbacks();
};
