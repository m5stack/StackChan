#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include "audio_types.h"
#include "protocol.h"

struct AudioCodecWork {
    bool stopped = false;
    bool decode_ready = false;
    bool encode_ready = false;
};

class AudioBus {
public:
    void Start();
    void Stop();

    bool IsStopped() const;
    bool IsIdle() const;

    void ClearAll();
    void ClearForDecoderReset();
    void MoveTestingToDecode();
    void WaitForPlaybackQueueEmpty() const;

    bool PushEncode(std::unique_ptr<AudioPcmFrame> frame, size_t max_queue_size);
    bool PushDecode(std::unique_ptr<AudioStreamPacket> packet, bool wait, size_t max_queue_size);
    void PushPlayback(std::unique_ptr<AudioPcmFrame> frame);
    void PushSend(std::unique_ptr<AudioStreamPacket> packet);
    void PushTesting(std::unique_ptr<AudioStreamPacket> packet);

    std::unique_ptr<AudioStreamPacket> PopSend();
    bool PopDecode(std::unique_ptr<AudioStreamPacket>& packet);
    bool PopEncode(std::unique_ptr<AudioPcmFrame>& frame);
    std::unique_ptr<AudioPcmFrame> WaitPopPlayback();
    AudioCodecWork WaitForCodecWork(size_t max_playback_queue_size, size_t max_send_queue_size);

    size_t TestingQueueSize() const;
    void PushTimestamp(uint32_t timestamp);
    void AttachTimestamp(AudioPcmFrame& frame, size_t max_timestamp_queue_size);

private:
    void ClearAllLocked();

    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::atomic_bool stopped_ = true;

    std::deque<std::unique_ptr<AudioStreamPacket>> decode_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> send_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> testing_queue_;
    std::deque<std::unique_ptr<AudioPcmFrame>> encode_queue_;
    std::deque<std::unique_ptr<AudioPcmFrame>> playback_queue_;
    std::deque<uint32_t> timestamp_queue_;
};
