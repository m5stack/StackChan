#include "esp_wake_word.h"

#include <esp_log.h>

namespace {

constexpr char kTag[] = "EspWakeWord";

}  // namespace

EspWakeWord::~EspWakeWord()
{
    if (wakenet_data_ != nullptr) {
        wakenet_iface_->destroy(wakenet_data_);
        esp_srmodel_deinit(wakenet_model_);
    }
}

bool EspWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list)
{
    codec_ = codec;
    wakenet_model_ = models_list == nullptr ? esp_srmodel_init("model") : models_list;
    if (wakenet_model_ == nullptr || wakenet_model_->num <= 0) {
        ESP_LOGE(kTag, "Failed to initialize wakenet model");
        return false;
    }
    if (wakenet_model_->num > 1) {
        ESP_LOGW(kTag, "More than one model found, using the first one");
    }

    char* model_name = wakenet_model_->model_name[0];
    wakenet_iface_ = esp_wn_handle_from_name(model_name);
    wakenet_data_ = wakenet_iface_->create(model_name, DET_MODE_95);

    const int frequency = wakenet_iface_->get_samp_rate(wakenet_data_);
    const int chunk_size = wakenet_iface_->get_samp_chunksize(wakenet_data_);
    ESP_LOGI(kTag, "Wake word(%s), freq: %d, chunksize: %d", model_name, frequency, chunk_size);
    return true;
}

void EspWakeWord::Feed(const std::vector<int16_t>& data)
{
    if (wakenet_data_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    if (!running_) {
        return;
    }

    if (codec_->input_channels() == 2) {
        for (size_t i = 0; i < data.size(); i += 2) {
            input_buffer_.push_back(data[i]);
        }
    } else {
        input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    }

    const int chunk_size = wakenet_iface_->get_samp_chunksize(wakenet_data_);
    while (input_buffer_.size() >= static_cast<size_t>(chunk_size)) {
        const int result = wakenet_iface_->detect(wakenet_data_, input_buffer_.data());
        if (result > 0) {
            last_detected_wake_word_ = wakenet_iface_->get_word_name(wakenet_data_, result);
            running_ = false;
            input_buffer_.clear();
            if (wake_word_detected_callback_ != nullptr) {
                wake_word_detected_callback_(last_detected_wake_word_);
            }
            break;
        }
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunk_size);
    }
}

void EspWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback)
{
    wake_word_detected_callback_ = std::move(callback);
}

void EspWakeWord::Start()
{
    running_ = true;
}

void EspWakeWord::Stop()
{
    running_ = false;
    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    input_buffer_.clear();
}

size_t EspWakeWord::GetFeedSize()
{
    if (wakenet_data_ == nullptr) {
        return 0;
    }
    return wakenet_iface_->get_samp_chunksize(wakenet_data_);
}

void EspWakeWord::EncodeWakeWordData()
{
}

bool EspWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus)
{
    (void)opus;
    return false;
}

const std::string& EspWakeWord::GetLastDetectedWakeWord() const
{
    return last_detected_wake_word_;
}
