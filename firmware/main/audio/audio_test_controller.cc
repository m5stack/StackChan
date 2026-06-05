#include "audio_test_controller.h"

#include "audio_bus.h"

AudioTestController::AudioTestController(AudioBus& bus)
    : bus_(bus)
{
}

void AudioTestController::Start()
{
    running_ = true;
}

void AudioTestController::Stop()
{
    running_ = false;
    bus_.MoveTestingToDecode();
}

void AudioTestController::Reset()
{
    running_ = false;
}

bool AudioTestController::IsRunning() const
{
    return running_;
}

bool AudioTestController::IsFull() const
{
    return bus_.TestingQueueSize() >= kMaxTestingFrames;
}
