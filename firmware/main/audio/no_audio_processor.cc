#include "no_audio_processor.h"

#include <esp_log.h>

namespace {

constexpr char kTag[] = "NoAudioProcessor";
constexpr int kMonoInputChannels = 1;
constexpr int kStereoInputChannels = 2;
constexpr int kVoiceSampleRateHz = 16000;

}  // namespace

void NoAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list)
{
    (void)models_list;
    codec_ = codec;
    frame_samples_ = (frame_duration_ms * kVoiceSampleRateHz) / 1000;
    ResetFrameState();
}

void NoAudioProcessor::Feed(std::vector<int16_t>&& data)
{
    if (!is_running_ || output_callback_ == nullptr || codec_ == nullptr || frame_samples_ <= 0) {
        return;
    }

    AppendMonoSamples(data);
    EmitCompleteFrames();
}

void NoAudioProcessor::Start()
{
    ResetFrameState();
    is_running_ = true;
}

void NoAudioProcessor::Stop()
{
    is_running_ = false;
    ResetFrameState();
}

bool NoAudioProcessor::IsRunning()
{
    return is_running_.load();
}

void NoAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback)
{
    output_callback_ = std::move(callback);
}

void NoAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback)
{
    vad_state_change_callback_ = std::move(callback);
}

size_t NoAudioProcessor::GetFeedSize()
{
    return frame_samples_ > 0 ? static_cast<size_t>(frame_samples_) : 0;
}

void NoAudioProcessor::EnableDeviceAec(bool enable)
{
    if (enable) {
        ESP_LOGW(kTag, "Device AEC requested on pass-through processor; request ignored");
    }
}

void NoAudioProcessor::ResetFrameState()
{
    pending_samples_.clear();
    if (frame_samples_ > 0) {
        pending_samples_.reserve(static_cast<size_t>(frame_samples_));
    }
}

void NoAudioProcessor::AppendMonoSamples(const std::vector<int16_t>& data)
{
    const int channels = codec_->input_channels();
    if (channels == kStereoInputChannels) {
        pending_samples_.reserve(pending_samples_.size() + (data.size() / kStereoInputChannels));
        for (size_t offset = 0; offset + 1 < data.size(); offset += kStereoInputChannels) {
            pending_samples_.push_back(data[offset]);
        }
        return;
    }

    if (channels != kMonoInputChannels) {
        ESP_LOGW(kTag, "Unexpected input channel count %d, treating data as mono", channels);
    }
    pending_samples_.insert(pending_samples_.end(), data.begin(), data.end());
}

void NoAudioProcessor::EmitCompleteFrames()
{
    const size_t frame_size = static_cast<size_t>(frame_samples_);
    while (pending_samples_.size() >= frame_size) {
        std::vector<int16_t> frame;
        frame.reserve(frame_size);
        frame.insert(frame.end(), pending_samples_.begin(), pending_samples_.begin() + frame_samples_);
        pending_samples_.erase(pending_samples_.begin(), pending_samples_.begin() + frame_samples_);
        output_callback_(std::move(frame));
    }
}
