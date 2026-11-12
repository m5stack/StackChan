"""Tests for :mod:`stackchan_mcp.stt.audio_utils` (Issue #91).

The decode helper is best exercised via a round-trip against the
existing TTS encoder when libopus is available; without libopus the
module still imports cleanly and only fails when actually called, so
the import-side behaviour is tested too.
"""

from __future__ import annotations

import pytest

from stackchan_mcp.stt.audio_utils import (
    DEVICE_CHANNELS,
    DEVICE_FRAME_DURATION_MS,
    DEVICE_SAMPLE_RATE,
    SAMPLES_PER_FRAME,
    decode_opus_frames,
)


def _import_opuslib_or_skip():
    try:
        return pytest.importorskip("opuslib")
    except Exception as exc:
        pytest.skip(f"opuslib unavailable: {exc}")


def test_device_audio_constants_match_tts():
    """The STT and TTS sides agree on the device wire parameters.

    Mismatched constants would silently produce garbled audio in both
    directions; pin them to the TTS module so any future change has
    to update both.
    """
    from stackchan_mcp.tts.audio_utils import (
        DEVICE_CHANNELS as TTS_CHANNELS,
        DEVICE_FRAME_DURATION_MS as TTS_FRAME_MS,
        DEVICE_SAMPLE_RATE as TTS_RATE,
        SAMPLES_PER_FRAME as TTS_SAMPLES,
    )

    assert DEVICE_SAMPLE_RATE == TTS_RATE == 16000
    assert DEVICE_CHANNELS == TTS_CHANNELS == 1
    assert DEVICE_FRAME_DURATION_MS == TTS_FRAME_MS == 60
    assert SAMPLES_PER_FRAME == TTS_SAMPLES == 960


def test_decode_opus_frames_empty_iterable_returns_empty_bytes():
    """Decoding zero frames is a no-op rather than an error."""
    _import_opuslib_or_skip()
    assert decode_opus_frames([]) == b""


def test_decode_opus_frames_skips_empty_frame():
    """An empty (zero-byte) frame is skipped, not passed to the decoder."""
    _import_opuslib_or_skip()
    assert decode_opus_frames([b""]) == b""


def test_decode_opus_roundtrip_with_real_libopus():
    """Encode silence with opuslib, then decode it back through our helper.

    The exact byte equality after a lossy round-trip isn't realistic,
    but the decoded PCM should match the expected sample count
    (one frame's worth per encoded frame) — that proves the framing
    parameters (sample rate, frame duration, channels) are all in sync.
    """
    opuslib = _import_opuslib_or_skip()

    encoder = opuslib.Encoder(
        DEVICE_SAMPLE_RATE, DEVICE_CHANNELS, opuslib.APPLICATION_VOIP
    )
    samples_per_frame = SAMPLES_PER_FRAME
    silence_pcm_frame = b"\x00\x00" * samples_per_frame

    opus_frames = []
    for _ in range(3):
        opus_frames.append(encoder.encode(silence_pcm_frame, samples_per_frame))

    decoded_pcm = decode_opus_frames(opus_frames)

    # 3 frames * 960 samples/frame * 2 bytes/sample = 5760 bytes.
    expected_bytes = 3 * samples_per_frame * 2
    assert len(decoded_pcm) == expected_bytes


def test_decode_opus_frames_skips_corrupt_frame():
    """A frame that fails to decode is logged and skipped, not raised.

    A malformed inbound packet should not abort the whole listen()
    call — partial transcription beats a hard failure on the wire.
    """
    opuslib = _import_opuslib_or_skip()

    encoder = opuslib.Encoder(
        DEVICE_SAMPLE_RATE, DEVICE_CHANNELS, opuslib.APPLICATION_VOIP
    )
    silence = encoder.encode(
        b"\x00\x00" * SAMPLES_PER_FRAME, SAMPLES_PER_FRAME
    )

    decoded = decode_opus_frames([silence, b"\x99\x99\x99\xff", silence])

    # Two valid frames decoded; the corrupt one was skipped.
    expected_bytes = 2 * SAMPLES_PER_FRAME * 2
    assert len(decoded) == expected_bytes
