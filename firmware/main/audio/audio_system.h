#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <model_path.h>

#include "audio_types.h"

class AudioCaptureWorker;
class AudioCodec;
class AudioBus;
class AudioPowerController;
class AudioDebugger;
class AudioPlaybackWorker;
class AudioTestController;
class OpusCodecWorker;
class VoiceProcessorController;
class WakeWordController;
struct AudioStreamPacket;

#define OPUS_FRAME_DURATION_MS 60
#define MAX_ENCODE_TASKS_IN_QUEUE 2
#define MAX_PLAYBACK_TASKS_IN_QUEUE 2
#define MAX_DECODE_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)
#define MAX_SEND_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)
#define AUDIO_TESTING_MAX_DURATION_MS 10000
#define MAX_TIMESTAMPS_IN_QUEUE 3

struct AudioSystemCallbacks {
    std::function<void()> on_send_queue_available;
    std::function<void(const std::string&)> on_wake_word_detected;
    std::function<void(bool)> on_vad_change;
};

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();

    void SetCallbacks(const AudioSystemCallbacks& callbacks);
    void SetModelsList(srmodel_list_t* models_list);

    void PlaySound(const std::string_view& ogg);
    void ResetDecoder();

    void EncodeWakeWord();
    std::unique_ptr<AudioStreamPacket> PopWakeWordPacket();
    const std::string& GetLastWakeWord() const;

    bool ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples);
    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait = false);
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();

    void EnableWakeWordDetection(bool enable);
    void EnableVoiceProcessing(bool enable);
    void EnableAudioTesting(bool enable);
    void EnableDeviceAec(bool enable);

    bool IsVoiceDetected() const;
    bool IsIdle();
    void WaitForPlaybackQueueEmpty();
    bool IsWakeWordRunning() const;
    bool IsAudioProcessorRunning() const;
    bool SupportsConcurrentVoiceSessionWakeDetection() const;

private:
    AudioCodec* codec_ = nullptr;
    std::unique_ptr<AudioBus> audio_bus_;
    std::unique_ptr<AudioPowerController> audio_power_controller_;
    std::unique_ptr<AudioTestController> audio_test_controller_;
    std::unique_ptr<VoiceProcessorController> voice_processor_controller_;
    std::unique_ptr<WakeWordController> wake_word_controller_;
    std::unique_ptr<AudioCaptureWorker> audio_capture_worker_;
    std::unique_ptr<AudioPlaybackWorker> audio_playback_worker_;
    std::unique_ptr<OpusCodecWorker> opus_codec_worker_;
    AudioSystemCallbacks callbacks_;
    srmodel_list_t* models_list_ = nullptr;

    bool wake_word_initialized_ = false;
    bool audio_processor_initialized_ = false;
    bool initialized_ = false;
    std::atomic_bool service_stopped_ = true;

    bool EnsureVoiceProcessorInitialized();
    bool EnsureWakeWordInitialized();
    void QueueEncodeWork(AudioFrameTarget target, std::vector<int16_t>&& pcm);
};
