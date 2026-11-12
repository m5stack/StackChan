"""Audio utilities for the TTS pipeline.

The helpers here decode WAV blobs, resample to the device's 16 kHz
sample rate, slice PCM into fixed-size frames, and encode those frames
to Opus. Each helper is independent so callers (and tests) can compose
them piecemeal.

``opuslib`` is imported lazily inside :func:`encode_opus_frames` so that
the rest of the module stays usable in environments where the ``[tts]``
extra is not installed (e.g. unit tests for resampling).

Device-side Opus parameters come from
``firmware/main/audio/audio_service.h``::

    sample_rate         = 16000 Hz
    channels            = 1
    frame_duration_ms   = 60
    samples_per_frame   = sample_rate * frame_duration_ms / 1000 = 960
"""

from __future__ import annotations

import array
import io
import logging
import wave
from typing import Iterator

logger = logging.getLogger(__name__)


#: Opus sample rate the device decoder is configured for.
DEVICE_SAMPLE_RATE = 16000

#: Opus channel count (mono).
DEVICE_CHANNELS = 1

#: Opus frame duration in milliseconds.
DEVICE_FRAME_DURATION_MS = 60

#: PCM samples per Opus frame at the device's settings (= 960).
SAMPLES_PER_FRAME = DEVICE_SAMPLE_RATE * DEVICE_FRAME_DURATION_MS // 1000


def wav_to_pcm16_mono(wav_bytes: bytes) -> tuple[int, bytes]:
    """Decode a WAV blob into ``(sample_rate, raw_pcm)``.

    The PCM is returned as signed 16-bit little-endian mono. Stereo
    inputs are mixed down by averaging L+R; anything other than 16-bit
    PCM raises :class:`ValueError` because we don't carry an audio
    library beyond the Python stdlib at this layer.

    Returning the source sample rate lets the caller decide whether to
    invoke :func:`resample_pcm16_linear`.
    """
    with wave.open(io.BytesIO(wav_bytes), "rb") as wav:
        n_channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        n_frames = wav.getnframes()
        raw = wav.readframes(n_frames)

    if sample_width != 2:
        raise ValueError(
            f"Unsupported WAV sample width {sample_width * 8}-bit "
            "(expected 16-bit signed PCM)"
        )

    if n_channels == 1:
        return sample_rate, raw

    if n_channels == 2:
        samples = array.array("h")
        samples.frombytes(raw)
        mono = array.array(
            "h",
            [
                (samples[i] + samples[i + 1]) // 2
                for i in range(0, len(samples), 2)
            ],
        )
        return sample_rate, mono.tobytes()

    raise ValueError(f"Unsupported channel count {n_channels} (expected 1 or 2)")


def resample_pcm16_linear(pcm: bytes, src_rate: int, dst_rate: int) -> bytes:
    """Linear-interpolation resample of signed-16-bit mono PCM.

    Linear interpolation is good enough for speech and keeps scipy out
    of the dependency tree, so the ``[tts]`` extra remains light. For
    music or fidelity-critical use we'd want a polyphase resampler, but
    that's not in scope here.
    """
    if src_rate == dst_rate:
        return pcm

    samples = array.array("h")
    samples.frombytes(pcm)
    n_src = len(samples)
    if n_src == 0:
        return b""

    n_dst = max(1, n_src * dst_rate // src_rate)
    out = array.array("h")

    if n_dst == 1:
        out.append(samples[0])
        return out.tobytes()

    # Map output index to source index in [0, n_src - 1] so the last
    # output sample lines up with the last source sample. This avoids
    # off-by-one drift that would compound when chaining resamples.
    ratio = (n_src - 1) / (n_dst - 1)
    for i in range(n_dst):
        x = i * ratio
        idx = int(x)
        frac = x - idx
        if idx + 1 >= n_src:
            out.append(samples[-1])
            continue
        a = samples[idx]
        b = samples[idx + 1]
        # Round toward zero is fine for 16-bit speech.
        out.append(int(a + (b - a) * frac))
    return out.tobytes()


def chunk_pcm_into_frames(
    pcm: bytes,
    samples_per_frame: int = SAMPLES_PER_FRAME,
) -> Iterator[bytes]:
    """Slice signed-16-bit LE PCM into fixed-size frames.

    The tail is zero-padded so every yielded chunk is exactly
    ``samples_per_frame * 2`` bytes long — Opus encoders require a
    constant frame size.
    """
    bytes_per_frame = samples_per_frame * 2  # 16-bit
    if bytes_per_frame <= 0:
        raise ValueError("samples_per_frame must be positive")

    for i in range(0, len(pcm), bytes_per_frame):
        chunk = pcm[i : i + bytes_per_frame]
        if len(chunk) < bytes_per_frame:
            chunk = chunk + b"\x00" * (bytes_per_frame - len(chunk))
        yield chunk


def encode_opus_frames(
    pcm: bytes,
    *,
    sample_rate: int = DEVICE_SAMPLE_RATE,
    channels: int = DEVICE_CHANNELS,
    frame_duration_ms: int = DEVICE_FRAME_DURATION_MS,
) -> Iterator[bytes]:
    """Encode signed-16-bit LE PCM into Opus frames.

    ``opuslib`` is imported lazily so this module remains importable
    when the ``[tts]`` extra is not installed; the helper only fails
    when actually called. Callers receive a clear ``RuntimeError`` that
    points at the right install command.
    """
    try:
        import opuslib  # type: ignore[import-not-found]
    except ImportError as exc:  # pragma: no cover - exercised via integration
        raise RuntimeError(
            "opuslib is not installed. Install with "
            "'pip install stackchan-mcp[tts]' to enable Opus encoding."
        ) from exc

    samples_per_frame = sample_rate * frame_duration_ms // 1000
    encoder = opuslib.Encoder(sample_rate, channels, opuslib.APPLICATION_VOIP)

    for pcm_frame in chunk_pcm_into_frames(pcm, samples_per_frame):
        opus_frame = encoder.encode(pcm_frame, samples_per_frame)
        yield opus_frame


def _decode_mp3_to_pcm16_mono(mp3_bytes: bytes) -> tuple[int, bytes]:
    """Decode an MP3 blob into ``(sample_rate, raw_pcm)``.

    The PCM is returned as signed 16-bit little-endian mono. ``miniaudio``
    is imported lazily here so the rest of the TTS framework stays
    importable when no MP3-based engine extra is installed; the failure
    only surfaces when synthesis is actually attempted.

    miniaudio decodes natively to interleaved signed-16-bit samples, so
    we ask for mono directly and let it downmix; only the sample rate may
    differ from the device's, which the caller resamples.
    """
    try:
        import miniaudio  # type: ignore[import-not-found]
    except ImportError as exc:  # pragma: no cover - exercised via integration
        raise RuntimeError(
            "miniaudio is not installed. Install an MP3-based engine extra "
            "('pip install stackchan-mcp[tts-irodori]' or "
            "'pip install stackchan-mcp[tts-elevenlabs]') to enable it."
        ) from exc

    decoded = miniaudio.decode(
        mp3_bytes,
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=1,
    )
    return decoded.sample_rate, decoded.samples.tobytes()
