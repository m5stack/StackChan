#include "processors/afe_audio_processor.h"

#include <algorithm>
#include <utility>

#include <esp_log.h>

#include "audio_codec.h"

namespace {

constexpr char kTag[] = "AfeAudioProcessor";
constexpr int kProcessorSampleRate = 16000;

}  // namespace

AfeAudioProcessor::~AfeAudioProcessor()
{
    running_ = false;
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    if (owned_models_ != nullptr) {
        esp_srmodel_deinit(owned_models_);
    }
}

void AfeAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list)
{
    codec_ = codec;
    frame_samples_ = frame_duration_ms * kProcessorSampleRate / 1000;
    output_buffer_.reserve(frame_samples_);

    ConfigureModels(models_list);
    CreateAfe(BuildInputFormat());

    xTaskCreate(
        [](void* arg) {
            static_cast<AfeAudioProcessor*>(arg)->TaskLoop();
            vTaskDelete(nullptr);
        },
        "afe_processor", 4096, this, 3, nullptr);
}

std::string AfeAudioProcessor::BuildInputFormat() const
{
    std::string format;
    if (codec_ == nullptr) {
        return format;
    }

    const int reference_channels = codec_->input_reference() ? 1 : 0;
    const int microphone_channels = std::max(0, codec_->input_channels() - reference_channels);
    format.append(static_cast<size_t>(microphone_channels), 'M');
    format.append(static_cast<size_t>(reference_channels), 'R');
    return format;
}

void AfeAudioProcessor::ConfigureModels(srmodel_list_t* models_list)
{
    srmodel_list_t* models = models_list;
    if (models == nullptr) {
        owned_models_ = esp_srmodel_init("model");
        models = owned_models_;
    }

    if (models != nullptr) {
        ns_model_name_ = esp_srmodel_filter(models, ESP_NSNET_PREFIX, nullptr);
        vad_model_name_ = esp_srmodel_filter(models, ESP_VADN_PREFIX, nullptr);
    }
}

void AfeAudioProcessor::CreateAfe(const std::string& input_format)
{
    afe_config_t* config = afe_config_init(input_format.c_str(), nullptr, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    config->vad_mode = VAD_MODE_0;
    config->vad_min_noise_ms = 100;
    config->vad_model_name = vad_model_name_;
    config->agc_init = false;
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    if (ns_model_name_ != nullptr) {
        config->ns_init = true;
        config->ns_model_name = ns_model_name_;
        config->afe_ns_mode = AFE_NS_MODE_NET;
    } else {
        config->ns_init = false;
    }

#ifdef CONFIG_USE_DEVICE_AEC
    config->aec_init = true;
    config->vad_init = false;
#else
    config->aec_init = false;
    config->vad_init = true;
#endif

    afe_iface_ = esp_afe_handle_from_config(config);
    afe_data_ = afe_iface_ != nullptr ? afe_iface_->create_from_config(config) : nullptr;
    if (afe_data_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create AFE processor");
    }
}

void AfeAudioProcessor::Start()
{
    running_ = true;
}

void AfeAudioProcessor::Stop()
{
    running_ = false;
    std::lock_guard lock(input_mutex_);
    input_buffer_.clear();
    output_buffer_.clear();
    speaking_ = false;
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
}

bool AfeAudioProcessor::IsRunning()
{
    return running_;
}

size_t AfeAudioProcessor::GetFeedSize()
{
    return afe_data_ != nullptr ? afe_iface_->get_feed_chunksize(afe_data_) : 0;
}

void AfeAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback)
{
    output_callback_ = std::move(callback);
}

void AfeAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback)
{
    vad_state_change_callback_ = std::move(callback);
}

void AfeAudioProcessor::Feed(std::vector<int16_t>&& data)
{
    if (!running_ || afe_data_ == nullptr || codec_ == nullptr) {
        return;
    }

    std::lock_guard lock(input_mutex_);
    if (!running_) {
        return;
    }
    input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    FeedBufferedChunksLocked();
}

void AfeAudioProcessor::FeedBufferedChunksLocked()
{
    const size_t chunk_samples = afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
    while (input_buffer_.size() >= chunk_samples) {
        afe_iface_->feed(afe_data_, input_buffer_.data());
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunk_samples);
    }
}

void AfeAudioProcessor::TaskLoop()
{
    while (true) {
        if (!running_ || afe_data_ == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        afe_fetch_result_t* result = afe_iface_->fetch_with_delay(afe_data_, kFetchTimeout);
        if (!running_ || result == nullptr) {
            continue;
        }
        if (result->ret_value == ESP_FAIL) {
            ESP_LOGW(kTag, "AFE fetch failed");
            continue;
        }

        EmitVadChange(result->vad_state);
        EmitProcessedAudio(result->data, result->data_size / sizeof(int16_t));
    }
}

void AfeAudioProcessor::EmitVadChange(vad_state_t state)
{
    if (!vad_state_change_callback_) {
        return;
    }

    const bool now_speaking = state == VAD_SPEECH;
    if (now_speaking == speaking_) {
        return;
    }
    speaking_ = now_speaking;
    vad_state_change_callback_(speaking_);
}

void AfeAudioProcessor::EmitProcessedAudio(const int16_t* data, size_t samples)
{
    if (!output_callback_ || data == nullptr || samples == 0) {
        return;
    }

    output_buffer_.insert(output_buffer_.end(), data, data + samples);
    while (output_buffer_.size() >= static_cast<size_t>(frame_samples_)) {
        std::vector<int16_t> frame(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
        output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
        output_callback_(std::move(frame));
    }
}

void AfeAudioProcessor::EnableDeviceAec(bool enable)
{
    if (afe_data_ == nullptr) {
        return;
    }

    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        afe_iface_->disable_vad(afe_data_);
        afe_iface_->enable_aec(afe_data_);
#else
        ESP_LOGW(kTag, "Device AEC requested but not configured");
#endif
        return;
    }

    afe_iface_->disable_aec(afe_data_);
    afe_iface_->enable_vad(afe_data_);
}
