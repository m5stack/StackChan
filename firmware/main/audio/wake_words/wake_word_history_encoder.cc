#include "wake_word_history_encoder.h"

#include "opus_config.h"

#include <esp_audio_enc.h>
#include <esp_audio_types.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_opus_enc.h>
#include <esp_timer.h>

#include <new>
#include <utility>

namespace {

constexpr int kFrameDurationMs = 60;

void CloseEncoder(void* handle)
{
    if (handle != nullptr) {
        esp_opus_enc_close(handle);
    }
}

}  // namespace

struct WakeWordHistoryEncoder::EncodeJob {
    WakeWordHistoryEncoder* owner = nullptr;
    std::deque<std::vector<int16_t>> frames;
};

WakeWordHistoryEncoder::WakeWordHistoryEncoder(const char* tag, size_t max_frames, size_t stack_size_bytes)
    : tag_(tag),
      max_frames_(max_frames),
      stack_size_bytes_(stack_size_bytes)
{
}

WakeWordHistoryEncoder::~WakeWordHistoryEncoder()
{
    if (task_stack_ != nullptr) {
        heap_caps_free(task_stack_);
    }
    if (task_buffer_ != nullptr) {
        heap_caps_free(task_buffer_);
    }
}

void WakeWordHistoryEncoder::Append(const int16_t* samples, size_t sample_count)
{
    if (samples == nullptr || sample_count == 0 || max_frames_ == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pcm_.emplace_back(samples, samples + sample_count);
    while (pcm_.size() > max_frames_) {
        pcm_.pop_front();
    }
}

void WakeWordHistoryEncoder::Append(const std::vector<int16_t>& samples)
{
    Append(samples.data(), samples.size());
}

void WakeWordHistoryEncoder::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pcm_.clear();
    opus_.clear();
}

void WakeWordHistoryEncoder::Start()
{
    auto* job = new (std::nothrow) EncodeJob;
    if (job == nullptr) {
        PublishEnd();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (encoding_) {
            delete job;
            return;
        }
        encoding_ = true;
        opus_.clear();
        job->owner = this;
        job->frames.swap(pcm_);
    }

    if (task_stack_ == nullptr) {
        task_stack_ = static_cast<StackType_t*>(heap_caps_malloc(stack_size_bytes_, MALLOC_CAP_SPIRAM));
    }
    if (task_buffer_ == nullptr) {
        task_buffer_ = static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL));
    }
    if (task_stack_ == nullptr || task_buffer_ == nullptr) {
        ESP_LOGE(tag_, "Failed to allocate wake-word encode task");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            encoding_ = false;
        }
        delete job;
        PublishEnd();
        return;
    }

    task_ = xTaskCreateStatic(TaskEntry, "ww_encode", stack_size_bytes_, job, 2, task_stack_, task_buffer_);
    if (task_ == nullptr) {
        ESP_LOGE(tag_, "Failed to start wake-word encode task");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            encoding_ = false;
        }
        delete job;
        PublishEnd();
    }
}

bool WakeWordHistoryEncoder::Pop(std::vector<uint8_t>& packet)
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return !opus_.empty(); });
    packet.swap(opus_.front());
    opus_.pop_front();
    return !packet.empty();
}

void WakeWordHistoryEncoder::TaskEntry(void* arg)
{
    auto* job = static_cast<EncodeJob*>(arg);
    if (job != nullptr && job->owner != nullptr) {
        job->owner->RunEncodeJob(*job);
    }
    delete job;
    vTaskDelete(nullptr);
}

void WakeWordHistoryEncoder::RunEncodeJob(EncodeJob& job)
{
    const int64_t start_us = esp_timer_get_time();
    int packet_count = 0;
    void* encoder = nullptr;

    auto config = audio::opus::MakeEncoderConfig(kFrameDurationMs);
    esp_audio_err_t result = esp_opus_enc_open(&config, sizeof(config), &encoder);
    if (result != ESP_AUDIO_ERR_OK || encoder == nullptr) {
        ESP_LOGE(tag_, "Failed to create wake-word encoder: %d", result);
        PublishEnd();
        std::lock_guard<std::mutex> lock(mutex_);
        encoding_ = false;
        task_ = nullptr;
        return;
    }

    int frame_bytes = 0;
    int out_capacity = 0;
    esp_opus_enc_get_frame_size(encoder, &frame_bytes, &out_capacity);
    const int frame_samples = frame_bytes / static_cast<int>(sizeof(int16_t));
    if (frame_samples <= 0 || out_capacity <= 0) {
        ESP_LOGE(tag_, "Invalid wake-word encoder frame sizing");
        CloseEncoder(encoder);
        PublishEnd();
        std::lock_guard<std::mutex> lock(mutex_);
        encoding_ = false;
        task_ = nullptr;
        return;
    }

    std::vector<int16_t> pcm;
    for (auto& frame : job.frames) {
        pcm.insert(pcm.end(), frame.begin(), frame.end());
        while (static_cast<int>(pcm.size()) >= frame_samples) {
            std::vector<uint8_t> encoded(out_capacity);
            esp_audio_enc_in_frame_t in = {
                .buffer = reinterpret_cast<uint8_t*>(pcm.data()),
                .len = static_cast<uint32_t>(frame_samples * sizeof(int16_t)),
            };
            esp_audio_enc_out_frame_t out = {
                .buffer = encoded.data(),
                .len = static_cast<uint32_t>(encoded.size()),
                .encoded_bytes = 0,
            };

            result = esp_opus_enc_process(encoder, &in, &out);
            if (result == ESP_AUDIO_ERR_OK && out.encoded_bytes > 0) {
                encoded.resize(out.encoded_bytes);
                Publish(std::move(encoded));
                ++packet_count;
            } else if (result != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(tag_, "Wake-word encode frame failed: %d", result);
            }
            pcm.erase(pcm.begin(), pcm.begin() + frame_samples);
        }
    }

    CloseEncoder(encoder);
    const int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGI(tag_, "Encoded wake-word history: packets=%d elapsed_ms=%lld", packet_count, elapsed_ms);

    PublishEnd();
    std::lock_guard<std::mutex> lock(mutex_);
    encoding_ = false;
    task_ = nullptr;
}

void WakeWordHistoryEncoder::Publish(std::vector<uint8_t>&& packet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    opus_.push_back(std::move(packet));
    cv_.notify_all();
}

void WakeWordHistoryEncoder::PublishEnd()
{
    Publish({});
}
