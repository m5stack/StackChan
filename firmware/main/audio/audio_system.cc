#include "audio_system.h"

#include <cstring>

#include <esp_log.h>

#include "audio_bus.h"
#include "audio_capture_worker.h"
#include "audio_codec.h"
#include "audio_playback_worker.h"
#include "audio_power_controller.h"
#include "audio_test_controller.h"
#include "ogg_demuxer.h"
#include "opus_codec_worker.h"
#include "voice_processor_controller.h"
#include "wake_word_controller.h"

namespace {

constexpr char kTag[] = "AudioSystem";

}  // namespace

AudioSystem::AudioSystem()
{
    audio_bus_ = std::make_unique<AudioBus>();
    audio_power_controller_ = std::make_unique<AudioPowerController>();
    audio_test_controller_ = std::make_unique<AudioTestController>(*audio_bus_);
    voice_processor_controller_ = std::make_unique<VoiceProcessorController>();
    wake_word_controller_ = std::make_unique<WakeWordController>();
}

AudioSystem::~AudioSystem()
{
    Stop();
}

void AudioSystem::Initialize(AudioCodec* codec)
{
    codec_ = codec;
    codec_->Start();
    audio_power_controller_->Initialize(codec_);

    voice_processor_controller_->SetModelsList(models_list_);
    voice_processor_controller_->SetOutputCallback([this](std::vector<int16_t>&& data) {
        QueueEncodeWork(AudioFrameTarget::kSend, std::move(data));
    });
    voice_processor_controller_->SetVadStateCallback([this](bool speaking) {
        if (callbacks_.on_vad_change != nullptr) {
            callbacks_.on_vad_change(speaking);
        }
    });

    wake_word_controller_->SetModelsList(models_list_);
    wake_word_controller_->SetWakeWordDetectedCallback([this](const std::string& wake_word) {
        if (callbacks_.on_wake_word_detected != nullptr) {
            callbacks_.on_wake_word_detected(wake_word);
        }
    });

    audio_capture_worker_ = std::make_unique<AudioCaptureWorker>(
        *codec_, *audio_power_controller_, *wake_word_controller_, *voice_processor_controller_,
        *audio_test_controller_, *audio_bus_);
    audio_capture_worker_->SetEncodePcmCallback([this](AudioFrameTarget target, std::vector<int16_t>&& pcm) {
        QueueEncodeWork(target, std::move(pcm));
    });
    if (!audio_capture_worker_->Initialize()) {
        ESP_LOGE(kTag, "Failed to initialize capture worker");
        initialized_ = false;
        return;
    }

    audio_playback_worker_ =
        std::make_unique<AudioPlaybackWorker>(*audio_bus_, *codec_, *audio_power_controller_);
    opus_codec_worker_ = std::make_unique<OpusCodecWorker>(*audio_bus_, *codec_);
    opus_codec_worker_->SetSendQueueAvailableCallback([this]() {
        if (callbacks_.on_send_queue_available != nullptr) {
            callbacks_.on_send_queue_available();
        }
    });
    if (!opus_codec_worker_->Initialize()) {
        ESP_LOGE(kTag, "Failed to initialize Opus worker");
        initialized_ = false;
        return;
    }

    initialized_ = true;
}

void AudioSystem::Start()
{
    if (!service_stopped_) {
        return;
    }
    if (!initialized_) {
        ESP_LOGE(kTag, "Audio system start rejected: not initialized");
        return;
    }

    service_stopped_ = false;
    audio_bus_->Start();
    audio_test_controller_->Reset();
    audio_power_controller_->Start();

    if (!audio_playback_worker_->Start()) {
        ESP_LOGE(kTag, "Failed to start audio playback worker");
        Stop();
        return;
    }
    if (!opus_codec_worker_->Start()) {
        ESP_LOGE(kTag, "Failed to start Opus codec worker");
        Stop();
        return;
    }
    if (!audio_capture_worker_->Start()) {
        ESP_LOGE(kTag, "Failed to start audio capture worker");
        Stop();
        return;
    }
}

void AudioSystem::Stop()
{
    if (service_stopped_) {
        return;
    }

    service_stopped_ = true;
    wake_word_controller_->Stop();
    voice_processor_controller_->Stop();
    audio_test_controller_->Reset();

    audio_capture_worker_->Stop();
    audio_playback_worker_->Stop();
    opus_codec_worker_->Stop();
    audio_bus_->Stop();

    audio_capture_worker_->WaitStopped(pdMS_TO_TICKS(2000));
    audio_playback_worker_->WaitStopped(pdMS_TO_TICKS(2000));
    opus_codec_worker_->WaitStopped(pdMS_TO_TICKS(2000));

    audio_power_controller_->Stop();
}

void AudioSystem::SetCallbacks(const AudioSystemCallbacks& callbacks)
{
    callbacks_ = callbacks;
}

void AudioSystem::SetModelsList(srmodel_list_t* models_list)
{
    models_list_ = models_list;

    if (audio_capture_worker_ != nullptr) {
        audio_capture_worker_->EnableWakeWord(false);
        audio_capture_worker_->EnableVoiceProcessing(false);
    }
    wake_word_controller_->Stop();
    voice_processor_controller_->Stop();

    wake_word_initialized_ = false;
    audio_processor_initialized_ = false;
    voice_processor_controller_->SetModelsList(models_list_);
    wake_word_controller_->SetModelsList(models_list_);

    ESP_LOGI(kTag, "Updated audio models list%s", service_stopped_ ? " while stopped" : " while running");
}

void AudioSystem::PlaySound(const std::string_view& ogg)
{
    audio_power_controller_->EnsureOutputActive();

    auto demuxer = std::make_unique<OggDemuxer>();
    demuxer->OnDemuxerFinished([this](const uint8_t* data, int sample_rate, size_t size) {
        auto packet = std::make_unique<AudioStreamPacket>();
        packet->sample_rate = sample_rate;
        packet->frame_duration = OPUS_FRAME_DURATION_MS;
        packet->payload.resize(size);
        std::memcpy(packet->payload.data(), data, size);
        PushPacketToDecodeQueue(std::move(packet), true);
    });

    demuxer->Reset();
    demuxer->Process(reinterpret_cast<const uint8_t*>(ogg.data()), ogg.size());
}

void AudioSystem::ResetDecoder()
{
    opus_codec_worker_->ResetDecoder();
}

void AudioSystem::EncodeWakeWord()
{
    wake_word_controller_->EncodeWakeWordData();
}

std::unique_ptr<AudioStreamPacket> AudioSystem::PopWakeWordPacket()
{
    if (!wake_word_controller_->HasEngine()) {
        return nullptr;
    }

    auto packet = std::make_unique<AudioStreamPacket>();
    if (!wake_word_controller_->GetWakeWordOpus(packet->payload)) {
        return nullptr;
    }
    return packet;
}

const std::string& AudioSystem::GetLastWakeWord() const
{
    return wake_word_controller_->GetLastDetectedWakeWord();
}

bool AudioSystem::ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples)
{
    return audio_capture_worker_ != nullptr && audio_capture_worker_->ReadAudioData(data, sample_rate, samples);
}

bool AudioSystem::PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait)
{
    return audio_bus_->PushDecode(std::move(packet), wait, MAX_DECODE_PACKETS_IN_QUEUE);
}

std::unique_ptr<AudioStreamPacket> AudioSystem::PopPacketFromSendQueue()
{
    return audio_bus_->PopSend();
}

void AudioSystem::EnableWakeWordDetection(bool enable)
{
    if (!enable) {
        audio_capture_worker_->EnableWakeWord(false);
        wake_word_controller_->Stop();
        return;
    }
    if (!EnsureWakeWordInitialized()) {
        return;
    }

    audio_capture_worker_->ResetInputResampler();
    wake_word_controller_->Start();
    audio_capture_worker_->EnableWakeWord(true);
}

void AudioSystem::EnableVoiceProcessing(bool enable)
{
    if (!enable) {
        audio_capture_worker_->EnableVoiceProcessing(false);
        voice_processor_controller_->Stop();
        return;
    }
    if (!EnsureVoiceProcessorInitialized()) {
        return;
    }

    opus_codec_worker_->ResetDecoder();
    audio_capture_worker_->RequestWarmup();
    audio_capture_worker_->ResetInputResampler();
    voice_processor_controller_->Start();
    audio_capture_worker_->EnableVoiceProcessing(true);
}

void AudioSystem::EnableAudioTesting(bool enable)
{
    ESP_LOGI(kTag, "%s audio testing", enable ? "Enabling" : "Disabling");
    if (enable) {
        audio_test_controller_->Start();
        return;
    }
    audio_test_controller_->Stop();
}

void AudioSystem::EnableDeviceAec(bool enable)
{
    if (!EnsureVoiceProcessorInitialized()) {
        return;
    }
    voice_processor_controller_->EnableDeviceAec(enable);
}

bool AudioSystem::IsVoiceDetected() const
{
    return voice_processor_controller_->IsVoiceDetected();
}

bool AudioSystem::IsIdle()
{
    return audio_bus_->IsIdle();
}

void AudioSystem::WaitForPlaybackQueueEmpty()
{
    audio_bus_->WaitForPlaybackQueueEmpty();
}

bool AudioSystem::IsWakeWordRunning() const
{
    return wake_word_controller_->IsRunning();
}

bool AudioSystem::IsAudioProcessorRunning() const
{
    return voice_processor_controller_->IsRunning();
}

bool AudioSystem::SupportsConcurrentVoiceSessionWakeDetection() const
{
    return wake_word_controller_->SupportsConcurrentVoiceSessionDetection();
}

bool AudioSystem::EnsureVoiceProcessorInitialized()
{
    if (audio_processor_initialized_) {
        return true;
    }
    if (!voice_processor_controller_->Initialize(codec_, OPUS_FRAME_DURATION_MS)) {
        ESP_LOGE(kTag, "Failed to initialize audio processor");
        return false;
    }
    audio_processor_initialized_ = true;
    return true;
}

bool AudioSystem::EnsureWakeWordInitialized()
{
    if (!wake_word_controller_->HasEngine()) {
        return false;
    }
    if (wake_word_initialized_) {
        return true;
    }
    if (!wake_word_controller_->Initialize(codec_)) {
        ESP_LOGE(kTag, "Failed to initialize wake word engine");
        return false;
    }
    wake_word_initialized_ = true;
    return true;
}

void AudioSystem::QueueEncodeWork(AudioFrameTarget target, std::vector<int16_t>&& pcm)
{
    auto work = std::make_unique<AudioPcmFrame>();
    work->target = target;
    work->pcm = std::move(pcm);
    if (target == AudioFrameTarget::kSend) {
        audio_bus_->AttachTimestamp(*work, MAX_TIMESTAMPS_IN_QUEUE);
    }
    audio_bus_->PushEncode(std::move(work), MAX_ENCODE_TASKS_IN_QUEUE);
}
