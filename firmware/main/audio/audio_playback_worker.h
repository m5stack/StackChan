#pragma once

#include "task_worker.h"

class AudioBus;
class AudioCodec;
class AudioPowerController;

class AudioPlaybackWorker final : public TaskWorker {
public:
    AudioPlaybackWorker(AudioBus& bus, AudioCodec& codec, AudioPowerController& power);

    bool Start();
    void Stop();

private:
    AudioBus& bus_;
    AudioCodec& codec_;
    AudioPowerController& power_;

    void Run() override;
};
