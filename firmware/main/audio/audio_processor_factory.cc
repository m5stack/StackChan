#include "audio_processor_factory.h"

#include "audio_processor.h"
#include "no_audio_processor.h"
#include "processors/afe_audio_processor.h"

std::unique_ptr<AudioProcessor> CreateDefaultAudioProcessor()
{
#if CONFIG_USE_AUDIO_PROCESSOR
    return std::make_unique<AfeAudioProcessor>();
#else
    return std::make_unique<NoAudioProcessor>();
#endif
}
