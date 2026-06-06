#include "afe_wake_word.h"

#include <esp_afe_config.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace {

constexpr const char* kTag = "AfeWakeWord";
constexpr size_t kHistoryFrames = 2000 / 30;
constexpr size_t kEncodeStackBytes = 4096 * 6;
constexpr TickType_t kDetectionPollTicks = pdMS_TO_TICKS(100);
constexpr TickType_t kIdlePollTicks = pdMS_TO_TICKS(50);

}  // namespace

AfeWakeWord::AfeWakeWord()
    : history_encoder_(kTag, kHistoryFrames, kEncodeStackBytes)
{
}

AfeWakeWord::~AfeWakeWord()
{
    stop_task_ = true;
    running_ = false;
    if (detection_task_ != nullptr) {
        vTaskDelete(detection_task_);
        detection_task_ = nullptr;
    }
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->destroy(afe_data_);
        afe_data_ = nullptr;
    }
    if (owns_models_ && models_ != nullptr) {
        esp_srmodel_deinit(models_);
        models_ = nullptr;
    }
}

bool AfeWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list)
{
    if (codec == nullptr) {
        ESP_LOGE(kTag, "Audio codec is required");
        return false;
    }

    codec_ = codec;
    models_ = models_list;
    owns_models_ = false;
    if (models_ == nullptr) {
        models_ = esp_srmodel_init("model");
        owns_models_ = true;
    }
    if (models_ == nullptr || models_->num <= 0) {
        ESP_LOGE(kTag, "No speech-recognition models available");
        return false;
    }
    if (!SelectWakeModel() || !CreateAfe()) {
        return false;
    }

    stop_task_ = false;
    if (xTaskCreate(DetectionTaskEntry, "afe_wake", 4096, this, 3, &detection_task_) != pdPASS) {
        ESP_LOGE(kTag, "Failed to start AFE wake detection task");
        return false;
    }
    return true;
}

void AfeWakeWord::Feed(const std::vector<int16_t>& data)
{
    if (!running_ || afe_data_ == nullptr || codec_ == nullptr || data.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_mutex_);
    if (!running_) {
        return;
    }

    input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    const size_t chunk_samples = static_cast<size_t>(afe_iface_->get_feed_chunksize(afe_data_)) * codec_->input_channels();
    while (chunk_samples > 0 && input_buffer_.size() >= chunk_samples) {
        afe_iface_->feed(afe_data_, input_buffer_.data());
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunk_samples);
    }
}

void AfeWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback)
{
    wake_word_detected_callback_ = std::move(callback);
}

void AfeWakeWord::Start()
{
    running_ = true;
}

void AfeWakeWord::Stop()
{
    running_ = false;
    std::lock_guard<std::mutex> lock(input_mutex_);
    input_buffer_.clear();
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
}

size_t AfeWakeWord::GetFeedSize()
{
    return afe_data_ != nullptr ? afe_iface_->get_feed_chunksize(afe_data_) : 0;
}

void AfeWakeWord::EncodeWakeWordData()
{
    history_encoder_.Start();
}

bool AfeWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus)
{
    return history_encoder_.Pop(opus);
}

void AfeWakeWord::DetectionTaskEntry(void* arg)
{
    auto* self = static_cast<AfeWakeWord*>(arg);
    self->DetectionLoop();
    self->detection_task_ = nullptr;
    vTaskDelete(nullptr);
}

bool AfeWakeWord::SelectWakeModel()
{
    wake_words_.clear();
    wake_model_ = nullptr;

    for (int i = 0; i < models_->num; ++i) {
        const char* model = models_->model_name[i];
        ESP_LOGI(kTag, "SR model %d: %s", i, model != nullptr ? model : "<null>");
        if (model != nullptr && std::strstr(model, ESP_WN_PREFIX) != nullptr) {
            wake_model_ = model;
            break;
        }
    }

    if (wake_model_ == nullptr) {
        ESP_LOGE(kTag, "No WakeNet model found");
        return false;
    }

    const char* words = esp_srmodel_get_wake_words(models_, const_cast<char*>(wake_model_));
    if (words != nullptr) {
        std::stringstream stream(words);
        std::string word;
        while (std::getline(stream, word, ';')) {
            if (!word.empty()) {
                wake_words_.push_back(word);
            }
        }
    }
    return true;
}

bool AfeWakeWord::CreateAfe()
{
    std::string input_format = BuildInputFormat();
    afe_config_t* config = afe_config_init(input_format.c_str(), models_, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (config == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate AFE config");
        return false;
    }

    config->aec_init = codec_->input_reference();
    config->aec_mode = AEC_MODE_SR_HIGH_PERF;
    config->afe_perferred_core = 1;
    config->afe_perferred_priority = 1;
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    afe_iface_ = esp_afe_handle_from_config(config);
    afe_data_ = afe_iface_ != nullptr ? afe_iface_->create_from_config(config) : nullptr;
    afe_config_free(config);

    if (afe_iface_ == nullptr || afe_data_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create AFE wake detector");
        return false;
    }
    return true;
}

std::string AfeWakeWord::BuildInputFormat() const
{
    const int input_channels = std::max(1, codec_->input_channels());
    const int ref_channels = codec_->input_reference() ? 1 : 0;
    const int mic_channels = std::max(1, input_channels - ref_channels);

    std::string format;
    format.append(static_cast<size_t>(mic_channels), 'M');
    format.append(static_cast<size_t>(ref_channels), 'R');
    return format;
}

void AfeWakeWord::DetectionLoop()
{
    while (!stop_task_) {
        if (!running_) {
            vTaskDelay(kIdlePollTicks);
            continue;
        }

        afe_fetch_result_t* result = afe_iface_->fetch_with_delay(afe_data_, kDetectionPollTicks);
        if (result == nullptr || result->ret_value == ESP_FAIL) {
            continue;
        }

        history_encoder_.Append(result->data, result->data_size / sizeof(int16_t));
        if (result->wakeup_state == WAKENET_DETECTED) {
            HandleDetection(result->wakenet_model_index);
        }
    }
}

void AfeWakeWord::HandleDetection(int model_index)
{
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_buffer_.clear();
    }

    const int word_index = model_index - 1;
    if (word_index >= 0 && static_cast<size_t>(word_index) < wake_words_.size()) {
        last_detected_wake_word_ = wake_words_[word_index];
    } else if (!wake_words_.empty()) {
        last_detected_wake_word_ = wake_words_.front();
    } else {
        last_detected_wake_word_ = "wake";
    }

    ESP_LOGI(kTag, "Wake word detected: %s", last_detected_wake_word_.c_str());
    if (wake_word_detected_callback_ != nullptr) {
        wake_word_detected_callback_(last_detected_wake_word_);
    }
}
