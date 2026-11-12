"""Audio utilities for the STT pipeline.

Mirror of :mod:`stackchan_mcp.tts.audio_utils` for the inbound direction:
the helpers here decode Opus frames coming up from the device and
concatenate them into a single PCM blob that a recogniser can consume.

``opuslib`` is imported lazily inside :func:`decode_opus_frames` so the
rest of the module stays usable in environments where the ``[stt]``
extra is not installed.

Device-side Opus parameters come from the firmware's hello handshake
(``firmware/main/protocols/websocket_protocol.cc::GetHelloMessage``)::

    sample_rate         = 16000 Hz
    channels            = 1
    frame_duration_ms   = OPUS_FRAME_DURATION_MS (60 ms)
    samples_per_frame   = sample_rate * frame_duration_ms / 1000 = 960
"""

from __future__ import annotations

import logging
from typing import Iterable

logger = logging.getLogger(__name__)


#: Opus sample rate the device encoder is configured for.
DEVICE_SAMPLE_RATE = 16000

#: Opus channel count (mono).
DEVICE_CHANNELS = 1

#: Opus frame duration in milliseconds (matches the firmware's
#: ``OPUS_FRAME_DURATION_MS``). Kept symmetric with
#: :data:`stackchan_mcp.tts.audio_utils.DEVICE_FRAME_DURATION_MS`.
DEVICE_FRAME_DURATION_MS = 60

#: PCM samples per Opus frame at the device's settings (= 960).
SAMPLES_PER_FRAME = DEVICE_SAMPLE_RATE * DEVICE_FRAME_DURATION_MS // 1000


class StreamingOpusDecoder:
    """Stateful decoder for sequential Opus frames from one device session."""

    def __init__(
        self,
        *,
        sample_rate: int = DEVICE_SAMPLE_RATE,
        channels: int = DEVICE_CHANNELS,
        frame_duration_ms: int = DEVICE_FRAME_DURATION_MS,
    ) -> None:
        try:
            import opuslib  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - exercised via integration
            raise RuntimeError(
                "opuslib is not installed. Install with "
                "'pip install stackchan-mcp[stt]' to enable Opus decoding."
            ) from exc

        self.samples_per_frame = sample_rate * frame_duration_ms // 1000
        self._decoder = opuslib.Decoder(sample_rate, channels)

    def decode_frame(self, frame: bytes) -> bytes:
        """Decode one Opus frame, preserving decoder state across calls."""
        if not frame:
            return b""
        return self._decoder.decode(frame, self.samples_per_frame)


def decode_opus_frames(
    frames: Iterable[bytes],
    *,
    sample_rate: int = DEVICE_SAMPLE_RATE,
    channels: int = DEVICE_CHANNELS,
    frame_duration_ms: int = DEVICE_FRAME_DURATION_MS,
) -> bytes:
    """Decode an iterable of Opus frames into a single PCM blob.

    Args:
        frames: Iterable of raw Opus payloads (i.e. the protocol v1
            wire format the firmware emits when ``protocol_version=1``;
            see :class:`stackchan_mcp.esp32_client.ESP32Connection`).
            Each frame must contain exactly ``frame_duration_ms`` of
            audio at ``sample_rate`` mono.
        sample_rate: Decoder sample rate (Hz). Defaults to the device's
            16 kHz.
        channels: Channel count. Defaults to mono.
        frame_duration_ms: Per-frame duration in ms. Defaults to the
            device's 60 ms cadence.

    Returns:
        Signed 16-bit little-endian PCM bytes concatenated across all
        frames. Frames that fail to decode are logged at warning level
        and skipped — partial transcription is better than failing the
        whole listen() call because one frame got mangled on the wire.

    Raises:
        RuntimeError: if ``opuslib`` is not installed. The error
            message points at the right install command so the caller
            can surface a clean MCP error.
    """
    decoder = StreamingOpusDecoder(
        sample_rate=sample_rate,
        channels=channels,
        frame_duration_ms=frame_duration_ms,
    )

    pcm_chunks: list[bytes] = []
    for index, frame in enumerate(frames):
        if not frame:
            continue
        try:
            pcm = decoder.decode_frame(frame)
        except Exception as exc:  # pragma: no cover - decode errors are rare
            logger.warning(
                "Opus decode failed for frame %d (size=%d): %s; skipping",
                index,
                len(frame),
                exc,
            )
            continue
        pcm_chunks.append(pcm)

    return b"".join(pcm_chunks)
