#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class OggDemuxer {
public:
    using PacketHandler = std::function<void(const uint8_t* data, int sample_rate, size_t len)>;

    OggDemuxer();

    void Reset();
    size_t Process(const uint8_t* data, size_t size);
    void OnDemuxerFinished(PacketHandler handler);

private:
    static constexpr size_t kCapturePatternSize = 4;
    static constexpr size_t kFixedHeaderSize = 27;
    static constexpr size_t kMaxBufferedInput = 96 * 1024;
    static constexpr size_t kMaxPacketSize = 16 * 1024;

    bool TryConsumePage();
    bool HandlePacket();
    void DropUntilNextCapturePattern();
    void DropConsumed(size_t bytes);

    std::vector<uint8_t> input_buffer_;
    std::vector<uint8_t> packet_buffer_;
    PacketHandler on_packet_;
    int sample_rate_ = 48000;
    bool saw_opus_head_ = false;
    bool saw_opus_tags_ = false;
};
