#include "wake_word_controller.h"

#include "audio_codec.h"
#include "wake_word.h"
#include "wake_word_factory.h"

void WakeWordController::SetModelsList(srmodel_list_t* models_list)
{
    models_list_ = models_list;
    RebuildEngine();
}

void WakeWordController::SetWakeWordDetectedCallback(std::function<void(const std::string& wake_word)> callback)
{
    on_wake_word_detected_ = std::move(callback);
    BindCallbacks();
}

bool WakeWordController::Initialize(AudioCodec* codec)
{
    if (wake_word_ == nullptr) {
        return false;
    }
    if (initialized_) {
        return true;
    }
    if (!wake_word_->Initialize(codec, models_list_)) {
        return false;
    }

    initialized_ = true;
    return true;
}

void WakeWordController::Start()
{
    if (wake_word_ != nullptr) {
        wake_word_->Start();
        running_ = true;
    }
}

void WakeWordController::Stop()
{
    if (wake_word_ != nullptr) {
        wake_word_->Stop();
    }
    running_ = false;
}

void WakeWordController::Feed(const std::vector<int16_t>& data)
{
    if (wake_word_ != nullptr) {
        wake_word_->Feed(data);
    }
}

bool WakeWordController::HasEngine() const
{
    return wake_word_ != nullptr;
}

bool WakeWordController::IsInitialized() const
{
    return initialized_;
}

bool WakeWordController::IsRunning() const
{
    return running_;
}

bool WakeWordController::SupportsConcurrentVoiceSessionDetection() const
{
    return wake_word_ != nullptr && wake_word_->SupportsConcurrentVoiceSessionDetection();
}

void WakeWordController::EncodeWakeWordData()
{
    if (wake_word_ != nullptr) {
        wake_word_->EncodeWakeWordData();
    }
}

bool WakeWordController::GetWakeWordOpus(std::vector<uint8_t>& opus) const
{
    return wake_word_ != nullptr && wake_word_->GetWakeWordOpus(opus);
}

const std::string& WakeWordController::GetLastDetectedWakeWord() const
{
    static const std::string kEmptyWakeWord;
    return wake_word_ != nullptr ? wake_word_->GetLastDetectedWakeWord() : kEmptyWakeWord;
}

void WakeWordController::RebuildEngine()
{
    running_ = false;
    wake_word_ = CreateWakeWordEngine(models_list_);
    initialized_ = false;
    BindCallbacks();
}

void WakeWordController::BindCallbacks()
{
    if (wake_word_ == nullptr) {
        return;
    }

    wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
        if (on_wake_word_detected_ != nullptr) {
            on_wake_word_detected_(wake_word);
        }
    });
}
