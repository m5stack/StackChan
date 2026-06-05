#pragma once

#include <memory>

class AudioProcessor;

std::unique_ptr<AudioProcessor> CreateDefaultAudioProcessor();
