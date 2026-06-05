#include "audio_capture_worker.h"

#include <esp_log.h>

#include "audio_bus.h"
#include "audio_codec.h"
#include "audio_power_controller.h"
#include "audio_test_controller.h"
#include "processors/audio_debugger.h"
#include "voice_processor_controller.h"
#include "wake_word_controller.h"

namespace {

constexpr char kTag[] = "AudioCapture";

esp_ae_rate_cvt_cfg_t MakeRateCvtConfig(int src_rate, int dest_rate, int channels)
{
    return {
        .src_rate = static_cast<uint32_t>(src_rate),
        .dest_rate = static_cast<uint32_t>(dest_rate),
        .channel = static_cast<uint8_t>(channels),
        .bits_per_sample = ESP_AE_BIT16,
        .complexity = 2,
        .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
    };
}

std::vector<int16_t> DownmixLeftChannel(std::vector<int16_t>&& data)
{
    std::vector<int16_t> mono(data.size() / 2);
    for (size_t i = 0, j = 0; i < mono.size(); ++i, j += 2) {
        mono[i] = data[j];
    }
    return mono;
}

}  // namespace

AudioCaptureWorker::AudioCaptureWorker(
    AudioCodec& codec,
    AudioPowerController& power,
    WakeWordController& wake_word,
    VoiceProcessorController& voice_processor,
    AudioTestController& test_controller,
    AudioBus& bus)
    : codec_(codec),
      power_(power),
      wake_word_(wake_word),
      voice_processor_(voice_processor),
      test_controller_(test_controller),
      bus_(bus)
{
}

AudioCaptureWorker::~AudioCaptureWorker()
{
    Stop();
    WaitStopped(pdMS_TO_TICKS(1000));
    DestroyInputResampler();
}

bool AudioCaptureWorker::Initialize()
{
    if (codec_.input_sample_rate() == kTargetInputSampleRate) {
        return true;
    }

    auto config = MakeRateCvtConfig(codec_.input_sample_rate(), kTargetInputSampleRate, codec_.input_channels());
    const auto result = esp_ae_rate_cvt_open(&config, &input_resampler_);
    if (input_resampler_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create input resampler: %d", result);
        return false;
    }
    return true;
}

bool AudioCaptureWorker::Start()
{
#if CONFIG_USE_AUDIO_PROCESSOR
    return StartTask("audio_input", 2048 * 3, 8, 0);
#else
    return StartTask("audio_input", 2048 * 2, 8);
#endif
}

void AudioCaptureWorker::Stop()
{
    RequestStop();
}

void AudioCaptureWorker::EnableWakeWord(bool enable)
{
    wake_word_enabled_ = enable;
}

void AudioCaptureWorker::EnableVoiceProcessing(bool enable)
{
    voice_processing_enabled_ = enable;
}

void AudioCaptureWorker::RequestWarmup()
{
    warmup_requested_ = true;
}

void AudioCaptureWorker::SetEncodePcmCallback(std::function<void(AudioFrameTarget, std::vector<int16_t>&&)> callback)
{
    on_encode_pcm_ = std::move(callback);
}

bool AudioCaptureWorker::ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples)
{
    power_.EnsureInputActive();

    if (codec_.input_sample_rate() == sample_rate) {
        data.resize(samples * codec_.input_channels());
        if (!codec_.InputData(data)) {
            return false;
        }
    } else {
        data.resize(samples * codec_.input_sample_rate() / sample_rate * codec_.input_channels());
        if (!codec_.InputData(data)) {
            return false;
        }
        if (input_resampler_ != nullptr) {
            std::lock_guard<std::mutex> lock(input_resampler_mutex_);
            uint32_t input_samples = data.size() / codec_.input_channels();
            uint32_t max_output_samples = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(input_resampler_, input_samples, &max_output_samples);
            std::vector<int16_t> converted(max_output_samples * codec_.input_channels());
            uint32_t actual_output_samples = max_output_samples;
            esp_ae_rate_cvt_process(
                input_resampler_,
                reinterpret_cast<esp_ae_sample_t*>(data.data()),
                input_samples,
                reinterpret_cast<esp_ae_sample_t*>(converted.data()),
                &actual_output_samples);
            converted.resize(actual_output_samples * codec_.input_channels());
            data = std::move(converted);
        }
    }

    power_.TouchInput();

#if CONFIG_USE_AUDIO_DEBUGGER
    if (audio_debugger_ == nullptr) {
        audio_debugger_ = std::make_unique<AudioDebugger>();
    }
    audio_debugger_->Feed(data);
#endif

    return true;
}

void AudioCaptureWorker::Run()
{
    while (!StopRequested()) {
        if (warmup_requested_) {
            warmup_requested_ = false;
            vTaskDelay(kWarmupDelay);
            continue;
        }

        if (test_controller_.IsRunning() && CaptureTestingFrame()) {
            continue;
        }
        if ((wake_word_enabled_ || voice_processing_enabled_) && CaptureRealtimeFrame()) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(kTag, "Audio capture worker stopped");
}

bool AudioCaptureWorker::CaptureTestingFrame()
{
    if (test_controller_.IsFull()) {
        ESP_LOGW(kTag, "Audio testing queue is full, disabling test mode");
        test_controller_.Stop();
        return true;
    }

    std::vector<int16_t> data;
    const int samples = kTestingFrameDurationMs * kTargetInputSampleRate / 1000;
    if (!ReadAudioData(data, kTargetInputSampleRate, samples)) {
        return false;
    }

    if (codec_.input_channels() == 2) {
        data = DownmixLeftChannel(std::move(data));
    }
    if (on_encode_pcm_ != nullptr) {
        on_encode_pcm_(AudioFrameTarget::kTesting, std::move(data));
    }
    return true;
}

bool AudioCaptureWorker::CaptureRealtimeFrame()
{
    std::vector<int16_t> data;
    if (!ReadAudioData(data, kTargetInputSampleRate, kRealtimeCaptureChunkSamples)) {
        return false;
    }

    if (wake_word_enabled_) {
        wake_word_.Feed(data);
    }
    if (voice_processing_enabled_) {
        voice_processor_.Feed(std::move(data));
    }
    return true;
}

void AudioCaptureWorker::ResetInputResampler()
{
    std::lock_guard<std::mutex> lock(input_resampler_mutex_);
    if (input_resampler_ != nullptr) {
        esp_ae_rate_cvt_reset(input_resampler_);
    }
}

void AudioCaptureWorker::DestroyInputResampler()
{
    std::lock_guard<std::mutex> lock(input_resampler_mutex_);
    if (input_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(input_resampler_);
        input_resampler_ = nullptr;
    }
}
