#include "audio_bus.h"

void AudioBus::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ClearAllLocked();
    stopped_ = false;
    cv_.notify_all();
}

void AudioBus::Stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    ClearAllLocked();
    cv_.notify_all();
}

bool AudioBus::IsStopped() const
{
    return stopped_;
}

bool AudioBus::IsIdle() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return encode_queue_.empty() && decode_queue_.empty() && playback_queue_.empty() && testing_queue_.empty();
}

void AudioBus::ClearAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ClearAllLocked();
    cv_.notify_all();
}

void AudioBus::ClearForDecoderReset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    timestamp_queue_.clear();
    decode_queue_.clear();
    playback_queue_.clear();
    testing_queue_.clear();
    cv_.notify_all();
}

void AudioBus::MoveTestingToDecode()
{
    std::lock_guard<std::mutex> lock(mutex_);
    decode_queue_ = std::move(testing_queue_);
    cv_.notify_all();
}

void AudioBus::WaitForPlaybackQueueEmpty() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() {
        return stopped_ || (decode_queue_.empty() && playback_queue_.empty());
    });
}

bool AudioBus::PushEncode(std::unique_ptr<AudioPcmFrame> frame, size_t max_queue_size)
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this, max_queue_size]() {
        return stopped_ || encode_queue_.size() < max_queue_size;
    });
    if (stopped_) {
        return false;
    }

    encode_queue_.push_back(std::move(frame));
    cv_.notify_all();
    return true;
}

bool AudioBus::PushDecode(std::unique_ptr<AudioStreamPacket> packet, bool wait, size_t max_queue_size)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!wait && decode_queue_.size() >= max_queue_size) {
        return false;
    }
    if (wait) {
        cv_.wait(lock, [this, max_queue_size]() {
            return stopped_ || decode_queue_.size() < max_queue_size;
        });
        if (stopped_) {
            return false;
        }
    }

    decode_queue_.push_back(std::move(packet));
    cv_.notify_all();
    return true;
}

void AudioBus::PushPlayback(std::unique_ptr<AudioPcmFrame> frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playback_queue_.push_back(std::move(frame));
    cv_.notify_all();
}

void AudioBus::PushSend(std::unique_ptr<AudioStreamPacket> packet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    send_queue_.push_back(std::move(packet));
    cv_.notify_all();
}

void AudioBus::PushTesting(std::unique_ptr<AudioStreamPacket> packet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    testing_queue_.push_back(std::move(packet));
    cv_.notify_all();
}

std::unique_ptr<AudioStreamPacket> AudioBus::PopSend()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (send_queue_.empty()) {
        return nullptr;
    }
    auto packet = std::move(send_queue_.front());
    send_queue_.pop_front();
    cv_.notify_all();
    return packet;
}

bool AudioBus::PopDecode(std::unique_ptr<AudioStreamPacket>& packet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (decode_queue_.empty()) {
        return false;
    }
    packet = std::move(decode_queue_.front());
    decode_queue_.pop_front();
    cv_.notify_all();
    return true;
}

bool AudioBus::PopEncode(std::unique_ptr<AudioPcmFrame>& frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (encode_queue_.empty()) {
        return false;
    }
    frame = std::move(encode_queue_.front());
    encode_queue_.pop_front();
    cv_.notify_all();
    return true;
}

std::unique_ptr<AudioPcmFrame> AudioBus::WaitPopPlayback()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() {
        return stopped_ || !playback_queue_.empty();
    });
    if (stopped_) {
        return nullptr;
    }

    auto frame = std::move(playback_queue_.front());
    playback_queue_.pop_front();
    cv_.notify_all();
    return frame;
}

AudioCodecWork AudioBus::WaitForCodecWork(size_t max_playback_queue_size, size_t max_send_queue_size)
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this, max_playback_queue_size, max_send_queue_size]() {
        const bool can_decode = !decode_queue_.empty() && playback_queue_.size() < max_playback_queue_size;
        const bool can_encode = !encode_queue_.empty() && send_queue_.size() < max_send_queue_size;
        return stopped_ || can_decode || can_encode;
    });

    AudioCodecWork work;
    work.stopped = stopped_;
    if (!work.stopped) {
        work.decode_ready = !decode_queue_.empty() && playback_queue_.size() < max_playback_queue_size;
        work.encode_ready = !encode_queue_.empty() && send_queue_.size() < max_send_queue_size;
    }
    return work;
}

size_t AudioBus::TestingQueueSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return testing_queue_.size();
}

void AudioBus::PushTimestamp(uint32_t timestamp)
{
    std::lock_guard<std::mutex> lock(mutex_);
    timestamp_queue_.push_back(timestamp);
}

void AudioBus::AttachTimestamp(AudioPcmFrame& frame, size_t max_timestamp_queue_size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (timestamp_queue_.empty()) {
        return;
    }
    if (timestamp_queue_.size() <= max_timestamp_queue_size) {
        frame.timestamp = timestamp_queue_.front();
    }
    timestamp_queue_.pop_front();
}

void AudioBus::ClearAllLocked()
{
    encode_queue_.clear();
    decode_queue_.clear();
    playback_queue_.clear();
    send_queue_.clear();
    testing_queue_.clear();
    timestamp_queue_.clear();
}
