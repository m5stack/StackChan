#pragma once

#include <cstdint>
#include <memory>
#include <vector>

enum class AudioFrameTarget {
    kSend,
    kTesting,
    kPlayback,
};

struct AudioPcmFrame {
    AudioFrameTarget target = AudioFrameTarget::kSend;
    std::vector<int16_t> pcm;
    uint32_t timestamp = 0;
};
