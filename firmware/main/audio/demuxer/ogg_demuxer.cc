#include "ogg_demuxer.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include <esp_log.h>

namespace {

constexpr char kTag[] = "OggDemuxer";
constexpr uint8_t kCapturePattern[] = {'O', 'g', 'g', 'S'};
constexpr uint8_t kOggVersion = 0;
constexpr uint8_t kContinuedPacketFlag = 0x01;

uint32_t ReadLe32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

OggDemuxer::OggDemuxer()
{
    Reset();
}

void OggDemuxer::Reset()
{
    input_buffer_.clear();
    packet_buffer_.clear();
    sample_rate_ = 48000;
    saw_opus_head_ = false;
    saw_opus_tags_ = false;
}

void OggDemuxer::OnDemuxerFinished(PacketHandler handler)
{
    on_packet_ = std::move(handler);
}

size_t OggDemuxer::Process(const uint8_t* data, size_t size)
{
    if (data == nullptr || size == 0) {
        return 0;
    }

    if (input_buffer_.size() + size > kMaxBufferedInput) {
        ESP_LOGW(kTag, "Dropping oversized Ogg input buffer");
        Reset();
    }

    input_buffer_.insert(input_buffer_.end(), data, data + size);
    while (TryConsumePage()) {
    }
    return size;
}

bool OggDemuxer::TryConsumePage()
{
    DropUntilNextCapturePattern();
    if (input_buffer_.size() < kFixedHeaderSize) {
        return false;
    }

    const uint8_t* header = input_buffer_.data();
    if (std::memcmp(header, kCapturePattern, sizeof(kCapturePattern)) != 0) {
        return true;
    }
    if (header[4] != kOggVersion) {
        ESP_LOGW(kTag, "Skipping unsupported Ogg page version: %u", header[4]);
        DropConsumed(kCapturePatternSize);
        return true;
    }

    const bool continued_from_previous_page = (header[5] & kContinuedPacketFlag) != 0;
    const size_t segment_count = header[26];
    const size_t segment_table_offset = kFixedHeaderSize;
    const size_t body_offset = segment_table_offset + segment_count;
    if (input_buffer_.size() < body_offset) {
        return false;
    }

    size_t body_size = 0;
    for (size_t i = 0; i < segment_count; ++i) {
        body_size += input_buffer_[segment_table_offset + i];
    }

    const size_t page_size = body_offset + body_size;
    if (input_buffer_.size() < page_size) {
        return false;
    }

    if (!continued_from_previous_page && !packet_buffer_.empty()) {
        ESP_LOGW(kTag, "Dropping incomplete Ogg packet at page boundary");
        packet_buffer_.clear();
    }

    size_t body_cursor = body_offset;
    for (size_t segment_index = 0; segment_index < segment_count; ++segment_index) {
        const uint8_t segment_size = input_buffer_[segment_table_offset + segment_index];
        if (packet_buffer_.size() + segment_size > kMaxPacketSize) {
            ESP_LOGW(kTag, "Dropping oversized Ogg packet");
            packet_buffer_.clear();
            DropConsumed(page_size);
            return true;
        }

        packet_buffer_.insert(packet_buffer_.end(), input_buffer_.begin() + body_cursor,
                              input_buffer_.begin() + body_cursor + segment_size);
        body_cursor += segment_size;

        if (segment_size < 255 && !HandlePacket()) {
            packet_buffer_.clear();
        }
    }

    DropConsumed(page_size);
    return true;
}

bool OggDemuxer::HandlePacket()
{
    if (packet_buffer_.empty()) {
        return true;
    }

    if (!saw_opus_head_) {
        if (packet_buffer_.size() >= 19 && std::memcmp(packet_buffer_.data(), "OpusHead", 8) == 0) {
            sample_rate_ = static_cast<int>(ReadLe32(packet_buffer_.data() + 12));
            saw_opus_head_ = true;
        }
        packet_buffer_.clear();
        return true;
    }

    if (!saw_opus_tags_) {
        if (packet_buffer_.size() >= 8 && std::memcmp(packet_buffer_.data(), "OpusTags", 8) == 0) {
            saw_opus_tags_ = true;
        }
        packet_buffer_.clear();
        return true;
    }

    if (on_packet_) {
        on_packet_(packet_buffer_.data(), sample_rate_, packet_buffer_.size());
    }
    packet_buffer_.clear();
    return true;
}

void OggDemuxer::DropUntilNextCapturePattern()
{
    auto it = std::search(input_buffer_.begin(), input_buffer_.end(),
                          std::begin(kCapturePattern), std::end(kCapturePattern));
    if (it == input_buffer_.begin()) {
        return;
    }
    if (it != input_buffer_.end()) {
        input_buffer_.erase(input_buffer_.begin(), it);
        return;
    }

    const size_t keep = std::min(input_buffer_.size(), kCapturePatternSize - 1);
    if (input_buffer_.size() > keep) {
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.end() - keep);
    }
}

void OggDemuxer::DropConsumed(size_t bytes)
{
    input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + std::min(bytes, input_buffer_.size()));
}
