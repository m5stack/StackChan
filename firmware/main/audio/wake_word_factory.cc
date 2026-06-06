#include "wake_word_factory.h"

#include "esp_wake_word.h"
#include "wake_word.h"
#include "wake_words/afe_custom_wake_word.h"
#include "wake_words/afe_wake_word.h"
#include "wake_words/custom_wake_word.h"

namespace {

class WakeWordAdapter final : public WakeWord {
public:
    WakeWordAdapter(std::unique_ptr<WakeWord> inner, bool supports_concurrent_voice_session_detection)
        : inner_(std::move(inner)),
          supports_concurrent_voice_session_detection_(supports_concurrent_voice_session_detection)
    {
    }

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list) override
    {
        return inner_ != nullptr && inner_->Initialize(codec, models_list);
    }

    void Feed(const std::vector<int16_t>& data) override
    {
        if (inner_ != nullptr) {
            inner_->Feed(data);
        }
    }

    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) override
    {
        if (inner_ != nullptr) {
            inner_->OnWakeWordDetected(std::move(callback));
        }
    }

    void Start() override
    {
        if (inner_ != nullptr) {
            inner_->Start();
        }
    }

    void Stop() override
    {
        if (inner_ != nullptr) {
            inner_->Stop();
        }
    }

    size_t GetFeedSize() override
    {
        return inner_ != nullptr ? inner_->GetFeedSize() : 0;
    }

    void EncodeWakeWordData() override
    {
        if (inner_ != nullptr) {
            inner_->EncodeWakeWordData();
        }
    }

    bool GetWakeWordOpus(std::vector<uint8_t>& opus) override
    {
        return inner_ != nullptr && inner_->GetWakeWordOpus(opus);
    }

    const std::string& GetLastDetectedWakeWord() const override
    {
        static const std::string kEmptyWakeWord;
        return inner_ != nullptr ? inner_->GetLastDetectedWakeWord() : kEmptyWakeWord;
    }

    bool SupportsConcurrentVoiceSessionDetection() const override
    {
        return supports_concurrent_voice_session_detection_;
    }

private:
    std::unique_ptr<WakeWord> inner_;
    bool supports_concurrent_voice_session_detection_ = false;
};

}  // namespace

std::unique_ptr<WakeWord> CreateWakeWordEngine(srmodel_list_t* models_list)
{
#if CONFIG_USE_REMOTE_WAKE_WORD
    (void)models_list;
    return nullptr;
#else
    if (models_list == nullptr) {
        return nullptr;
    }

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#if CONFIG_USE_CUSTOM_WAKE_WORD
    if (esp_srmodel_filter(models_list, ESP_MN_PREFIX, nullptr) != nullptr) {
        return std::make_unique<WakeWordAdapter>(std::make_unique<AfeCustomWakeWord>(), false);
    }
#elif CONFIG_USE_AFE_WAKE_WORD
    if (esp_srmodel_filter(models_list, ESP_WN_PREFIX, nullptr) != nullptr) {
        return std::make_unique<WakeWordAdapter>(std::make_unique<AfeWakeWord>(), true);
    }
#endif
#else
#if CONFIG_USE_ESP_WAKE_WORD
    if (esp_srmodel_filter(models_list, ESP_WN_PREFIX, nullptr) != nullptr) {
        return std::make_unique<EspWakeWord>();
    }
#endif
#endif

    return nullptr;
#endif
}
