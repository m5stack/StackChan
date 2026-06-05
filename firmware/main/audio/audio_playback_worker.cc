#include "audio_playback_worker.h"

#include <esp_log.h>

#include "audio_bus.h"
#include "audio_codec.h"
#include "audio_power_controller.h"

namespace {

constexpr char kTag[] = "AudioPlayback";

}  // namespace

AudioPlaybackWorker::AudioPlaybackWorker(AudioBus& bus, AudioCodec& codec, AudioPowerController& power)
    : bus_(bus),
      codec_(codec),
      power_(power)
{
}

bool AudioPlaybackWorker::Start()
{
#if CONFIG_USE_AUDIO_PROCESSOR
    return StartTask("audio_output", 2048 * 2, 4);
#else
    return StartTask("audio_output", 2048, 4);
#endif
}

void AudioPlaybackWorker::Stop()
{
    RequestStop();
}

void AudioPlaybackWorker::Run()
{
    while (!StopRequested()) {
        auto task = bus_.WaitPopPlayback();
        if (task == nullptr) {
            break;
        }

        power_.EnsureOutputActive();
        codec_.OutputData(task->pcm);
        power_.TouchOutput();

#if CONFIG_USE_SERVER_AEC
        if (task->timestamp > 0) {
            bus_.PushTimestamp(task->timestamp);
        }
#endif
    }

    ESP_LOGW(kTag, "Audio playback worker stopped");
}
