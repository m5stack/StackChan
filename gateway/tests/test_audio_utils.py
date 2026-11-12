"""Tests for the TTS audio helpers (Issue #70 PR2)."""

from __future__ import annotations

import array
import importlib

import pytest

from _audio_fixtures import make_wav_bytes
from stackchan_mcp.tts.audio_utils import (
    DEVICE_FRAME_DURATION_MS,
    DEVICE_SAMPLE_RATE,
    SAMPLES_PER_FRAME,
    chunk_pcm_into_frames,
    encode_opus_frames,
    resample_pcm16_linear,
    wav_to_pcm16_mono,
)


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------


def test_device_constants_match_firmware():
    """Device constants match firmware/main/audio/audio_service.h."""
    assert DEVICE_SAMPLE_RATE == 16000
    assert DEVICE_FRAME_DURATION_MS == 60
    assert SAMPLES_PER_FRAME == 960  # 16000 * 60 / 1000


# ---------------------------------------------------------------------------
# WAV decoding
# ---------------------------------------------------------------------------


def test_wav_to_pcm16_mono_roundtrip_mono():
    """Mono 16-bit PCM WAV decodes to the original bytes at the same rate."""
    samples = [100, -200, 300, -400, 500]
    wav = make_wav_bytes(sample_rate=24000, samples=samples)

    rate, pcm = wav_to_pcm16_mono(wav)

    assert rate == 24000
    decoded = array.array("h")
    decoded.frombytes(pcm)
    assert list(decoded) == samples


def test_wav_to_pcm16_mono_downmixes_stereo():
    """Stereo input is averaged into mono."""
    # L=1000, R=2000 -> mono should average to 1500
    samples = [1000, 2000, -1000, -2000]
    wav = make_wav_bytes(sample_rate=16000, channels=2, samples=samples)

    rate, pcm = wav_to_pcm16_mono(wav)

    decoded = array.array("h")
    decoded.frombytes(pcm)
    assert rate == 16000
    assert list(decoded) == [1500, -1500]


def test_wav_to_pcm16_mono_rejects_8bit():
    """Non-16-bit inputs raise ValueError rather than silently dropping bits."""
    import io
    import struct
    import wave

    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)  # 8-bit unsigned PCM
        wav.setframerate(16000)
        wav.writeframes(struct.pack("<10B", *([0x80] * 10)))

    with pytest.raises(ValueError, match="sample width"):
        wav_to_pcm16_mono(buf.getvalue())


def test_wav_to_pcm16_mono_rejects_unsupported_channels():
    """Three or more channels would need a real downmixer; we just raise."""
    import io
    import struct
    import wave

    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(3)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(struct.pack("<6h", *([0] * 6)))

    with pytest.raises(ValueError, match="channel count"):
        wav_to_pcm16_mono(buf.getvalue())


# ---------------------------------------------------------------------------
# Resampling
# ---------------------------------------------------------------------------


def test_resample_same_rate_returns_input_unchanged():
    """Equal rates skip the resample loop and return the same bytes."""
    samples = array.array("h", [10, 20, 30, 40, 50])
    pcm = samples.tobytes()

    out = resample_pcm16_linear(pcm, 16000, 16000)

    assert out == pcm


def test_resample_24khz_to_16khz_shrinks_by_2_3():
    """Downsampling 3:2 produces ~ 2/3 the samples."""
    samples = array.array("h", list(range(0, 30)))  # 30 samples
    pcm = samples.tobytes()

    out = resample_pcm16_linear(pcm, 24000, 16000)
    decoded = array.array("h")
    decoded.frombytes(out)

    # 30 * 16000 / 24000 = 20 samples
    assert len(decoded) == 20
    # Endpoints should line up with the source endpoints.
    assert decoded[0] == 0
    assert decoded[-1] == 29


def test_resample_8khz_to_16khz_grows_by_2x():
    """Upsampling 1:2 doubles the sample count."""
    samples = array.array("h", [0, 100, 200])
    pcm = samples.tobytes()

    out = resample_pcm16_linear(pcm, 8000, 16000)
    decoded = array.array("h")
    decoded.frombytes(out)

    # 3 * 16000 / 8000 = 6 samples
    assert len(decoded) == 6
    # Endpoints preserved.
    assert decoded[0] == 0
    assert decoded[-1] == 200


def test_resample_empty_input_returns_empty():
    """Empty PCM bytes are a no-op rather than a divide-by-zero."""
    assert resample_pcm16_linear(b"", 24000, 16000) == b""


def test_resample_single_sample_handled():
    """A single sample resamples to exactly one sample (avoids div-by-zero)."""
    pcm = array.array("h", [42]).tobytes()

    out = resample_pcm16_linear(pcm, 24000, 16000)
    decoded = array.array("h")
    decoded.frombytes(out)

    assert list(decoded) == [42]


# ---------------------------------------------------------------------------
# Frame chunking
# ---------------------------------------------------------------------------


def test_chunk_pcm_into_frames_yields_fixed_size():
    """Each yielded frame is exactly ``samples_per_frame * 2`` bytes."""
    pcm = b"\x00\x00" * 2400  # 2400 samples
    frames = list(chunk_pcm_into_frames(pcm, samples_per_frame=960))

    # 2400 / 960 = 2.5 -> 3 frames (with the third zero-padded)
    assert len(frames) == 3
    for frame in frames:
        assert len(frame) == 960 * 2


def test_chunk_pcm_into_frames_zero_pads_tail():
    """The trailing partial frame is zero-padded to the full size."""
    pcm = b"\x01\x00" * 100  # 100 non-zero samples, one short frame
    frames = list(chunk_pcm_into_frames(pcm, samples_per_frame=960))

    assert len(frames) == 1
    frame = frames[0]
    assert len(frame) == 960 * 2
    # First 100 samples non-zero, remaining zero-padded.
    assert frame[: 100 * 2] == pcm
    assert frame[100 * 2 :] == b"\x00" * (860 * 2)


def test_chunk_pcm_into_frames_empty_input_yields_nothing():
    """No PCM means no frames — not a single zero-padded frame."""
    assert list(chunk_pcm_into_frames(b"", samples_per_frame=960)) == []


def test_chunk_pcm_into_frames_rejects_non_positive_size():
    """A zero or negative samples_per_frame is a programmer error."""
    with pytest.raises(ValueError):
        list(chunk_pcm_into_frames(b"\x00" * 100, samples_per_frame=0))


# ---------------------------------------------------------------------------
# Opus encoding (gated on libopus availability)
# ---------------------------------------------------------------------------


def test_encode_opus_frames_produces_frames_when_libopus_available():
    """When libopus is reachable, encode_opus_frames yields one frame per chunk."""
    try:
        opuslib = importlib.import_module("opuslib")
        opuslib.Encoder(16000, 1, opuslib.APPLICATION_VOIP)
    except Exception as exc:  # libopus not loadable
        pytest.skip(f"libopus not available: {exc}")

    # 60 ms of silence at 16 kHz mono = 960 samples = 1920 bytes
    pcm = b"\x00\x00" * 960

    frames = list(encode_opus_frames(pcm))

    assert len(frames) == 1
    assert isinstance(frames[0], bytes)
    assert len(frames[0]) > 0
