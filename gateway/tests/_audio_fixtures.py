"""Shared helpers for TTS-related tests.

Builds in-memory WAV blobs without depending on any third-party audio
libraries — only the stdlib ``wave`` module is involved.
"""

from __future__ import annotations

import io
import struct
import wave
from collections.abc import Iterable


def make_wav_bytes(
    sample_rate: int = 24000,
    duration_ms: int = 100,
    channels: int = 1,
    samples: Iterable[int] | None = None,
) -> bytes:
    """Build a signed 16-bit LE WAV blob.

    Args:
        sample_rate: WAV sample rate.
        duration_ms: Duration; ignored when ``samples`` is given.
        channels: 1 or 2 channels.
        samples: Iterable of int16 sample values. When omitted, a
            silent block of the requested duration is produced. For
            stereo, samples are interleaved L/R/L/R.

    Returns:
        WAV blob suitable for feeding into :func:`wav_to_pcm16_mono`.
    """
    if samples is None:
        n_samples = sample_rate * duration_ms // 1000 * channels
        sample_values: list[int] = [0] * n_samples
    else:
        sample_values = list(samples)

    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(struct.pack(f"<{len(sample_values)}h", *sample_values))
    return buf.getvalue()
