#include "opus_codec_worker.h"

#include <esp_log.h>

#include "audio_bus.h"
#include "audio_codec.h"
#include "opus_config.h"
#include "protocol.h"

namespace {

constexpr char kTag[] = "OpusCodec";

esp_ae_rate_cvt_cfg_t MakeRateCvtConfig(int src_rate, int dest_rate, int channels)
{
    return {
        .src_rate = static_cast<uint32_t>(src_rate),
        .dest_rate = static_cast<uint32_t>(dest_rate),
        .channel = static_cast<uint8_t>(channels),
        .bits_per_sample = ESP_AUDIO_BIT16,
        .complexity = 2,
        .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
    };
}

esp_opus_dec_cfg_t MakeDecoderConfig(int sample_rate, int frame_duration_ms)
{
    return {
        .sample_rate = static_cast<uint32_t>(sample_rate),
        .channel = ESP_AUDIO_MONO,
        .frame_duration = static_cast<esp_opus_dec_frame_duration_t>(
            (frame_duration_ms == 5 ? ESP_OPUS_ENC_FRAME_DURATION_5_MS :
             frame_duration_ms == 10 ? ESP_OPUS_ENC_FRAME_DURATION_10_MS :
             frame_duration_ms == 20 ? ESP_OPUS_ENC_FRAME_DURATION_20_MS :
             frame_duration_ms == 40 ? ESP_OPUS_ENC_FRAME_DURATION_40_MS :
             frame_duration_ms == 60 ? ESP_OPUS_ENC_FRAME_DURATION_60_MS :
             frame_duration_ms == 80 ? ESP_OPUS_ENC_FRAME_DURATION_80_MS :
             frame_duration_ms == 100 ? ESP_OPUS_ENC_FRAME_DURATION_100_MS :
             frame_duration_ms == 120 ? ESP_OPUS_ENC_FRAME_DURATION_120_MS : -1)),
        .self_delimited = false,
    };
}

}  // namespace

OpusCodecWorker::OpusCodecWorker(AudioBus& bus, AudioCodec& codec)
    : bus_(bus),
      codec_(codec)
{
}

OpusCodecWorker::~OpusCodecWorker()
{
    Stop();
    WaitStopped(pdMS_TO_TICKS(1000));
}

bool OpusCodecWorker::Initialize()
{
    return encoder_.Open() && decoder_.Open(codec_.output_sample_rate(), kFrameDurationMs) &&
           output_resampler_.Configure(decoder_.sample_rate(), codec_.output_sample_rate(), ESP_AUDIO_MONO);
}

bool OpusCodecWorker::Start()
{
    return StartTask("opus_codec", 2048 * 12, 2);
}

void OpusCodecWorker::Stop()
{
    RequestStop();
}

void OpusCodecWorker::ResetDecoder()
{
    bus_.ClearForDecoderReset();
    decoder_.Reset();
}

void OpusCodecWorker::SetSendQueueAvailableCallback(std::function<void()> callback)
{
    on_send_queue_available_ = std::move(callback);
}

void OpusCodecWorker::Run()
{
    while (!StopRequested()) {
        const auto work = bus_.WaitForCodecWork(kMaxPlaybackTasksInQueue, kMaxSendPacketsInQueue);
        if (work.stopped) {
            break;
        }
        if (work.decode_ready) {
            HandleDecodeWork();
        }
        if (work.encode_ready) {
            HandleEncodeWork();
        }
    }

    ESP_LOGW(kTag, "Opus codec worker stopped");
}

void OpusCodecWorker::HandleDecodeWork()
{
    std::unique_ptr<AudioStreamPacket> packet;
    if (!bus_.PopDecode(packet)) {
        return;
    }
    if (!EnsureDecoderConfigured(packet->sample_rate, packet->frame_duration)) {
        return;
    }

    auto playback = std::make_unique<AudioPcmFrame>();
    playback->target = AudioFrameTarget::kPlayback;
    playback->timestamp = packet->timestamp;
    if (!decoder_.Decode(*packet, playback->pcm)) {
        ESP_LOGE(kTag, "Failed to decode audio");
        return;
    }

    if (decoder_.sample_rate() != codec_.output_sample_rate() && output_resampler_.IsConfigured()) {
        output_resampler_.Convert(playback->pcm);
    }
    bus_.PushPlayback(std::move(playback));
}

void OpusCodecWorker::HandleEncodeWork()
{
    std::unique_ptr<AudioPcmFrame> work;
    if (!bus_.PopEncode(work)) {
        return;
    }
    if (!encoder_.IsOpen() || work->pcm.size() != static_cast<size_t>(encoder_.frame_samples())) {
        ESP_LOGE(kTag, "Encoder unavailable or invalid frame size (%u vs %u)",
            static_cast<unsigned>(work->pcm.size()), static_cast<unsigned>(encoder_.frame_samples()));
        return;
    }

    auto packet = std::make_unique<AudioStreamPacket>();
    packet->sample_rate = encoder_.sample_rate();
    packet->frame_duration = encoder_.frame_duration_ms();
    packet->timestamp = work->timestamp;
    if (!encoder_.Encode(work->pcm, packet->payload)) {
        ESP_LOGE(kTag, "Failed to encode audio");
        return;
    }

    if (work->target == AudioFrameTarget::kSend) {
        bus_.PushSend(std::move(packet));
        if (on_send_queue_available_ != nullptr) {
            on_send_queue_available_();
        }
    } else {
        bus_.PushTesting(std::move(packet));
    }
}

bool OpusCodecWorker::EnsureDecoderConfigured(int sample_rate, int frame_duration_ms)
{
    if (decoder_.sample_rate() == sample_rate && decoder_.frame_duration_ms() == frame_duration_ms) {
        return true;
    }
    if (!decoder_.Open(sample_rate, frame_duration_ms)) {
        return false;
    }
    return output_resampler_.Configure(decoder_.sample_rate(), codec_.output_sample_rate(), ESP_AUDIO_MONO);
}

OpusCodecWorker::OpusEncoderHandle::~OpusEncoderHandle()
{
    Close();
}

bool OpusCodecWorker::OpusEncoderHandle::Open()
{
    Close();
    auto config = audio::opus::MakeEncoderConfig(kFrameDurationMs);
    const auto result = esp_opus_enc_open(&config, sizeof(config), &handle_);
    if (handle_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create Opus encoder: %d", result);
        return false;
    }

    esp_opus_enc_get_frame_size(handle_, &frame_samples_, &output_buffer_size_);
    frame_samples_ /= static_cast<int>(sizeof(int16_t));
    return true;
}

void OpusCodecWorker::OpusEncoderHandle::Close()
{
    if (handle_ != nullptr) {
        esp_opus_enc_close(handle_);
        handle_ = nullptr;
    }
    frame_samples_ = 0;
    output_buffer_size_ = 0;
}

bool OpusCodecWorker::OpusEncoderHandle::IsOpen() const
{
    return handle_ != nullptr;
}

bool OpusCodecWorker::OpusEncoderHandle::Encode(const std::vector<int16_t>& pcm, std::vector<uint8_t>& encoded) const
{
    if (handle_ == nullptr) {
        return false;
    }

    encoded.resize(output_buffer_size_);
    esp_audio_enc_in_frame_t in = {
        .buffer = reinterpret_cast<uint8_t*>(const_cast<int16_t*>(pcm.data())),
        .len = static_cast<uint32_t>(frame_samples_ * sizeof(int16_t)),
    };
    esp_audio_enc_out_frame_t out = {
        .buffer = encoded.data(),
        .len = static_cast<uint32_t>(encoded.size()),
        .encoded_bytes = 0,
    };
    const auto result = esp_opus_enc_process(handle_, &in, &out);
    if (result != ESP_AUDIO_ERR_OK) {
        return false;
    }

    encoded.resize(out.encoded_bytes);
    return true;
}

int OpusCodecWorker::OpusEncoderHandle::frame_samples() const
{
    return frame_samples_;
}

int OpusCodecWorker::OpusEncoderHandle::sample_rate() const
{
    return kEncoderSampleRate;
}

int OpusCodecWorker::OpusEncoderHandle::frame_duration_ms() const
{
    return kFrameDurationMs;
}

OpusCodecWorker::OpusDecoderHandle::~OpusDecoderHandle()
{
    Close();
}

bool OpusCodecWorker::OpusDecoderHandle::Open(int sample_rate, int frame_duration_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Close();
    auto config = MakeDecoderConfig(sample_rate, frame_duration_ms);
    const auto result = esp_opus_dec_open(&config, sizeof(config), &handle_);
    if (handle_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create Opus decoder: %d", result);
        return false;
    }

    sample_rate_ = sample_rate;
    frame_duration_ms_ = frame_duration_ms;
    frame_samples_ = sample_rate_ / 1000 * frame_duration_ms_;
    return true;
}

void OpusCodecWorker::OpusDecoderHandle::Close()
{
    if (handle_ != nullptr) {
        esp_opus_dec_close(handle_);
        handle_ = nullptr;
    }
    sample_rate_ = 0;
    frame_samples_ = 0;
}

void OpusCodecWorker::OpusDecoderHandle::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ != nullptr) {
        esp_opus_dec_reset(handle_);
    }
}

bool OpusCodecWorker::OpusDecoderHandle::IsOpen() const
{
    return handle_ != nullptr;
}

bool OpusCodecWorker::OpusDecoderHandle::Decode(const AudioStreamPacket& packet, std::vector<int16_t>& pcm) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ == nullptr) {
        return false;
    }

    pcm.resize(frame_samples_);
    esp_audio_dec_in_raw_t raw = {
        .buffer = const_cast<uint8_t*>(packet.payload.data()),
        .len = static_cast<uint32_t>(packet.payload.size()),
        .consumed = 0,
        .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
    };
    esp_audio_dec_out_frame_t out = {
        .buffer = reinterpret_cast<uint8_t*>(pcm.data()),
        .len = static_cast<uint32_t>(pcm.size() * sizeof(int16_t)),
        .decoded_size = 0,
    };
    esp_audio_dec_info_t info = {};
    const auto result = esp_opus_dec_decode(handle_, &raw, &out, &info);
    if (result != ESP_AUDIO_ERR_OK) {
        return false;
    }

    pcm.resize(out.decoded_size / sizeof(int16_t));
    return true;
}

int OpusCodecWorker::OpusDecoderHandle::sample_rate() const
{
    return sample_rate_;
}

int OpusCodecWorker::OpusDecoderHandle::frame_duration_ms() const
{
    return frame_duration_ms_;
}

int OpusCodecWorker::OpusDecoderHandle::frame_samples() const
{
    return frame_samples_;
}

OpusCodecWorker::RateConverterHandle::~RateConverterHandle()
{
    if (handle_ != nullptr) {
        esp_ae_rate_cvt_close(handle_);
    }
}

bool OpusCodecWorker::RateConverterHandle::Configure(int src_rate, int dest_rate, int channels)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (src_rate == dest_rate) {
        if (handle_ != nullptr) {
            esp_ae_rate_cvt_close(handle_);
            handle_ = nullptr;
        }
        src_rate_ = src_rate;
        dest_rate_ = dest_rate;
        channels_ = channels;
        return true;
    }

    if (handle_ != nullptr) {
        esp_ae_rate_cvt_close(handle_);
        handle_ = nullptr;
    }

    auto config = MakeRateCvtConfig(src_rate, dest_rate, channels);
    const auto result = esp_ae_rate_cvt_open(&config, &handle_);
    if (handle_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create output resampler: %d", result);
        return false;
    }

    src_rate_ = src_rate;
    dest_rate_ = dest_rate;
    channels_ = channels;
    return true;
}

void OpusCodecWorker::RateConverterHandle::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ != nullptr) {
        esp_ae_rate_cvt_reset(handle_);
    }
}

bool OpusCodecWorker::RateConverterHandle::IsConfigured() const
{
    return handle_ != nullptr;
}

void OpusCodecWorker::RateConverterHandle::Convert(std::vector<int16_t>& pcm) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ == nullptr) {
        return;
    }

    uint32_t max_output_samples = 0;
    esp_ae_rate_cvt_get_max_out_sample_num(handle_, pcm.size(), &max_output_samples);
    std::vector<int16_t> converted(max_output_samples);
    uint32_t actual_output_samples = max_output_samples;
    esp_ae_rate_cvt_process(
        handle_,
        reinterpret_cast<esp_ae_sample_t*>(pcm.data()),
        pcm.size(),
        reinterpret_cast<esp_ae_sample_t*>(converted.data()),
        &actual_output_samples);
    converted.resize(actual_output_samples);
    pcm = std::move(converted);
}
