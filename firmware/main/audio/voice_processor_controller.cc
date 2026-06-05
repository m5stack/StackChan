#include "voice_processor_controller.h"

#include "audio_codec.h"
#include "audio_processor.h"
#include "audio_processor_factory.h"

VoiceProcessorController::VoiceProcessorController()
    : processor_(CreateDefaultAudioProcessor())
{
    BindCallbacks();
}

void VoiceProcessorController::SetModelsList(srmodel_list_t* models_list)
{
    models_list_ = models_list;
}

void VoiceProcessorController::SetOutputCallback(std::function<void(std::vector<int16_t>&& data)> callback)
{
    on_output_ = std::move(callback);
    BindCallbacks();
}

void VoiceProcessorController::SetVadStateCallback(std::function<void(bool speaking)> callback)
{
    on_vad_state_change_ = std::move(callback);
    BindCallbacks();
}

bool VoiceProcessorController::Initialize(AudioCodec* codec, int frame_duration_ms)
{
    if (initialized_) {
        return true;
    }
    if (processor_ == nullptr) {
        return false;
    }

    frame_duration_ms_ = frame_duration_ms;
    processor_->Initialize(codec, frame_duration_ms_, models_list_);
    initialized_ = true;
    return true;
}

void VoiceProcessorController::Start()
{
    if (processor_ != nullptr) {
        processor_->Start();
    }
}

void VoiceProcessorController::Stop()
{
    if (processor_ != nullptr) {
        processor_->Stop();
    }
}

void VoiceProcessorController::Feed(std::vector<int16_t>&& data)
{
    if (processor_ != nullptr) {
        processor_->Feed(std::move(data));
    }
}

void VoiceProcessorController::EnableDeviceAec(bool enable)
{
    if (processor_ != nullptr) {
        processor_->EnableDeviceAec(enable);
    }
}

bool VoiceProcessorController::IsInitialized() const
{
    return initialized_;
}

bool VoiceProcessorController::IsRunning() const
{
    return processor_ != nullptr && processor_->IsRunning();
}

bool VoiceProcessorController::IsVoiceDetected() const
{
    return voice_detected_;
}

void VoiceProcessorController::BindCallbacks()
{
    if (processor_ == nullptr) {
        return;
    }

    processor_->OnOutput([this](std::vector<int16_t>&& data) {
        if (on_output_ != nullptr) {
            on_output_(std::move(data));
        }
    });
    processor_->OnVadStateChange([this](bool speaking) {
        voice_detected_ = speaking;
        if (on_vad_state_change_ != nullptr) {
            on_vad_state_change_(speaking);
        }
    });
}
