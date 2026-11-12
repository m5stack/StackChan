"""Device-driven listen audio forwarding to an external HTTP hook.

When the ESP32 device autonomously enters listening mode — wake word
detection (``WakeWordInvoke``), button press, or LCD touch
(``ToggleChatState``) — the gateway's MCP-driven STT pipeline is not
running because there is no concurrent ``listen()`` tool call to open a
recording slot. The inbound Opus frames are therefore discarded today
(see :mod:`stackchan_mcp.audio_stream` module docstring).

This module fills that gap. When configured with a hook URL, the
gateway opens a recording slot on inbound ``{"type":"listen",
"state":"start"}`` messages from the device, buffers the Opus frames
into the same module-level slot used by the MCP-driven path, packs them
into an Ogg/Opus container on ``{"state":"stop"}``, and POSTs the
payload to the hook.

Configuration:

- ``STACKCHAN_AUDIO_HOOK_URL`` — HTTP(S) URL of the receiver. The
  device-driven capture path is silently disabled when unset.
- ``STACKCHAN_AUDIO_HOOK_TOKEN`` — Bearer token; falls back to
  ``STACKCHAN_TOKEN`` so a single-token setup works without extra
  configuration.

The capture path is opt-in by design: stackchan-mcp's primary listen
model is MCP-client-driven (the ``listen()`` tool), and device-driven
capture only makes sense when an external service is set up to receive
the audio. Leaving the hook URL unset keeps the gateway's behaviour
unchanged from server-driven listen.

Ogg container implementation note: we assemble the container directly
in pure Python (RFC 3533 + RFC 7845) rather than pulling in pyogg or
similar. The format is well-specified and our inputs are fixed (single
stream, mono, 16 kHz, 60 ms Opus frames), so a 200-line implementation
keeps the dependency surface unchanged from the existing ``[stt]`` /
``[tts]`` extras (just ``opuslib`` for codec, no container library).
"""

from __future__ import annotations

import logging
import struct
from typing import Sequence

import aiohttp

logger = logging.getLogger(__name__)


# --- Device audio parameters -------------------------------------------------

#: Sample rate the firmware emits (matches the STT pipeline's expectation;
#: see :data:`stackchan_mcp.stt.audio_utils.DEVICE_SAMPLE_RATE`).
DEVICE_SAMPLE_RATE = 16000

#: Frame duration the firmware emits. 60 ms is the xiaozhi-esp32 default;
#: each WebSocket binary message carries exactly one Opus frame.
DEVICE_FRAME_DURATION_MS = 60

#: Number of audio samples per Opus frame, at the device sample rate.
SAMPLES_PER_FRAME = DEVICE_SAMPLE_RATE * DEVICE_FRAME_DURATION_MS // 1000  # 960

#: Opus granule positions are always expressed in 48 kHz samples, even when
#: the underlying stream is 16 kHz mono (RFC 7845 §4.1.7). So one 60 ms frame
#: advances the granule by 48000 * 60/1000 = 2880.
GRANULE_PER_FRAME = 48000 * DEVICE_FRAME_DURATION_MS // 1000  # 2880


# --- Ogg/Opus container ------------------------------------------------------
#
# Ogg page layout (RFC 3533 §6):
#   0..3   "OggS"
#   4      stream_structure_version (0)
#   5      header_type_flag (0x02 BOS, 0x04 EOS, 0x01 continued; can OR)
#   6..13  granule_position (int64 LE)
#   14..17 bitstream_serial_number (uint32 LE)
#   18..21 page_sequence_number (uint32 LE)
#   22..25 CRC32 (zeroed during calculation, then patched)
#   26     number_of_page_segments (1..255)
#   27..   segment_table (one byte per segment, 0..255 each)
#   ..     segment data (concatenated)
#
# CRC32 polynomial: 0x04C11DB7, MSB-first, no initial value, no final XOR.
# This differs from zlib.crc32; we precompute a table.

_OGG_MAGIC = b"OggS"
_OPUS_HEAD_MAGIC = b"OpusHead"
_OPUS_TAGS_MAGIC = b"OpusTags"

_HEADER_BOS = 0x02
_HEADER_EOS = 0x04
_HEADER_CONTINUED = 0x01


def _build_ogg_crc_table() -> list[int]:
    """Precompute the 256-entry CRC32 table for Ogg's MSB-first polynomial.

    Ogg's CRC is a "vanilla" 32-bit CRC (no reflection, no XOR-out) with
    polynomial 0x04C11DB7. zlib.crc32 uses the same polynomial but with
    bit-reflected input/output and XOR-out 0xFFFFFFFF, so we cannot reuse
    it directly.
    """
    poly = 0x04C11DB7
    table = []
    for byte in range(256):
        crc = byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ poly) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
        table.append(crc)
    return table


_OGG_CRC_TABLE = _build_ogg_crc_table()


def _ogg_crc32(data: bytes) -> int:
    """Compute Ogg's CRC32 over ``data`` (table-driven, MSB-first)."""
    crc = 0
    for byte in data:
        crc = ((crc << 8) ^ _OGG_CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]) & 0xFFFFFFFF
    return crc


def _packet_to_segments(packet: bytes) -> list[bytes]:
    """Split an Ogg packet into 255-byte lacing segments (RFC 3533 §6).

    Packets longer than 255 bytes are split into 255-byte runs; packets
    whose length is exactly a multiple of 255 are terminated with a
    zero-length segment so the parser knows the packet ended there
    (otherwise it would expect a continuation into the next page).
    Variable-bitrate Opus frames can exceed 255 bytes in practice, so
    this split must happen before page assembly.
    """
    segments: list[bytes] = []
    if not packet:
        # An empty packet is itself a single zero-length segment.
        return [b""]
    pos = 0
    n = len(packet)
    while pos < n:
        chunk = packet[pos:pos + 255]
        segments.append(chunk)
        pos += 255
    if len(packet) % 255 == 0:
        # Packet ends exactly on a 255-byte boundary — append a
        # terminating zero-length segment per RFC 3533.
        segments.append(b"")
    return segments


def _build_ogg_page(
    *,
    header_type: int,
    granule_position: int,
    serial: int,
    page_sequence: int,
    segments: Sequence[bytes],
) -> bytes:
    """Assemble one Ogg page from a list of segments (each ≤ 255 bytes).

    Caller is responsible for splitting larger packets into ≤ 255-byte
    segments and emitting a 0-byte terminating segment when a packet
    happens to end exactly on a 255-byte boundary (Ogg packetisation
    rule, RFC 3533 §6). For our 60 ms Opus frames at 16 kbps target
    this never fires — frames are well under 255 bytes — but we keep
    the API segment-oriented for correctness.
    """
    if len(segments) < 1 or len(segments) > 255:
        raise ValueError(
            f"Ogg page must have 1..255 segments, got {len(segments)}"
        )
    segment_table = bytes(len(s) for s in segments)
    for seg in segments:
        if len(seg) > 255:
            raise ValueError(
                f"Ogg segment exceeds 255 bytes ({len(seg)}); "
                "split into multiple segments before calling _build_ogg_page"
            )

    body = b"".join(segments)
    header = struct.pack(
        "<4sBBqII",
        _OGG_MAGIC,
        0,  # stream_structure_version
        header_type,
        granule_position,
        serial,
        page_sequence,
    )
    # CRC field placeholder (4 bytes of 0x00), then segment count and table.
    header_with_crc_placeholder = (
        header + b"\x00\x00\x00\x00" + bytes([len(segments)]) + segment_table
    )
    full_page = header_with_crc_placeholder + body
    crc = _ogg_crc32(full_page)
    # Patch CRC at offset 22.
    return full_page[:22] + struct.pack("<I", crc) + full_page[26:]


def _build_opus_head_packet(
    *,
    channels: int = 1,
    pre_skip: int = 0,
    input_sample_rate: int = DEVICE_SAMPLE_RATE,
) -> bytes:
    """OpusHead identification header packet (RFC 7845 §5.1)."""
    return struct.pack(
        "<8sBBHIhB",
        _OPUS_HEAD_MAGIC,
        1,                    # version
        channels,
        pre_skip,
        input_sample_rate,    # informational only; decoder always runs at 48 kHz
        0,                    # output_gain (Q7.8 fixed-point dB), 0 = unchanged
        0,                    # channel_mapping_family: 0 = mono/stereo
    )


def _build_opus_tags_packet(vendor: bytes = b"stackchan-mcp") -> bytes:
    """OpusTags comment header packet (RFC 7845 §5.2). Empty comment list."""
    return (
        _OPUS_TAGS_MAGIC
        + struct.pack("<I", len(vendor))
        + vendor
        + struct.pack("<I", 0)  # comment_count = 0
    )


# How many Opus frames to pack into a single audio page. The Ogg spec
# allows up to 255 segments per page (and our frames fit in one segment
# each at this bitrate). Smaller pages give finer-grained recovery on
# corruption but waste header bytes; 50 is a comfortable middle
# (~3 seconds of audio per page).
_FRAMES_PER_PAGE = 50


def pack_opus_frames_to_ogg(
    frames: Sequence[bytes],
    *,
    serial: int = 1,
    channels: int = 1,
    pre_skip: int = 0,
) -> bytes:
    """Pack raw Opus frames into a complete Ogg/Opus stream.

    Args:
        frames: One raw Opus packet per element, as emitted by the
            xiaozhi-esp32 firmware (one packet per WebSocket binary
            message). Empty input yields an empty bytes object so the
            caller can short-circuit "no audio" without raising.
        serial: Ogg bitstream serial number. Required to be present in
            every page; the value itself is opaque to the decoder,
            but uniqueness matters when multiplexing — we are not, so
            any non-zero value works.
        channels: 1 (mono) or 2 (stereo). The firmware sends mono.
        pre_skip: Samples to drop at the start of decoded output, in
            48 kHz units (RFC 7845 §5.1). 0 is the conservative default;
            a real encoder reports its actual look-ahead here.

    Returns:
        A bytes object containing the full Ogg/Opus stream (BOS page
        with OpusHead, page with OpusTags, one or more audio pages, the
        last marked EOS). Ready to be POSTed as ``audio/ogg``.
    """
    if not frames:
        return b""

    out = bytearray()
    page_seq = 0

    # Page 0: BOS with OpusHead.
    out += _build_ogg_page(
        header_type=_HEADER_BOS,
        granule_position=0,
        serial=serial,
        page_sequence=page_seq,
        segments=[_build_opus_head_packet(
            channels=channels,
            pre_skip=pre_skip,
            input_sample_rate=DEVICE_SAMPLE_RATE,
        )],
    )
    page_seq += 1

    # Page 1: OpusTags. RFC 7845 requires this as the second page,
    # before any audio data.
    out += _build_ogg_page(
        header_type=0,
        granule_position=0,
        serial=serial,
        page_sequence=page_seq,
        segments=[_build_opus_tags_packet()],
    )
    page_seq += 1

    # Audio pages: ``_FRAMES_PER_PAGE`` frames per page until the last
    # page, which is marked EOS regardless of fill.
    total_frames = len(frames)
    granule = 0
    for start in range(0, total_frames, _FRAMES_PER_PAGE):
        end = min(start + _FRAMES_PER_PAGE, total_frames)
        page_frames = frames[start:end]
        granule += len(page_frames) * GRANULE_PER_FRAME
        is_last_page = end == total_frames
        # Split each opus packet into Ogg lacing segments. VBR opus can
        # produce packets > 255 bytes, which Ogg encodes as multiple
        # 255-byte segments plus a trailing remainder; packets whose
        # length is an exact multiple of 255 need a zero-length
        # terminator (RFC 3533 §6). _build_ogg_page expects ≤ 255
        # segments per page, so a page's segment count can exceed
        # _FRAMES_PER_PAGE when individual frames have to be split.
        # Flush mid-batch when the segment table is about to overflow
        # so each emitted page stays inside the 255-segment limit.
        segments: list[bytes] = []
        for frame in page_frames:
            frame_segs = _packet_to_segments(frame)
            if len(segments) + len(frame_segs) > 255:
                out += _build_ogg_page(
                    header_type=0,  # continuation page
                    granule_position=granule,
                    serial=serial,
                    page_sequence=page_seq,
                    segments=segments,
                )
                page_seq += 1
                segments = []
            segments.extend(frame_segs)
        if segments:
            out += _build_ogg_page(
                header_type=_HEADER_EOS if is_last_page else 0,
                granule_position=granule,
                serial=serial,
                page_sequence=page_seq,
                segments=segments,
            )
            page_seq += 1

    return bytes(out)


# --- HTTP push --------------------------------------------------------------


async def push_audio_capture(
    hook_url: str,
    token: str,
    frames: Sequence[bytes],
    *,
    session_id: str = "",
    timeout_s: float = 10.0,
) -> bool:
    """POST a device-driven listen capture to the configured hook URL.

    Args:
        hook_url: Receiver URL (typically the SAIVerse-side
            ``audio_input_relay`` endpoint). Must be set.
        token: Bearer token for ``Authorization: Bearer <token>``.
            Empty string disables auth header (mirroring
            ``STACKCHAN_TOKEN`` semantics — gateway logs a warning at
            startup when the token is unset).
        frames: Raw Opus packets from the device for this listen window.
        session_id: ESP32 connection session ID, forwarded to the
            receiver via the ``X-StackChan-Session`` header so the
            receiver can correlate captures with vessel pairing.
        timeout_s: Total HTTP timeout (default 10s; an Ogg blob for a
            5-minute capture is well under 1 MB so this is generous).

    Returns:
        ``True`` if the POST returned 2xx, ``False`` otherwise (including
        on Ogg pack failure or network error). Failures are logged at
        WARNING; callers do not need to log again.
    """
    if not frames:
        logger.debug(
            "audio_input_hook: skipping push, no frames (session=%s)", session_id
        )
        return False

    try:
        ogg_payload = pack_opus_frames_to_ogg(frames)
    except Exception as exc:
        logger.warning(
            "audio_input_hook: Ogg pack failed for %d frames (session=%s): %s",
            len(frames), session_id, exc,
        )
        return False

    headers = {
        "Content-Type": "audio/ogg",
        "X-StackChan-Session": session_id,
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"

    try:
        async with aiohttp.ClientSession() as session:
            async with session.post(
                hook_url,
                data=ogg_payload,
                headers=headers,
                timeout=aiohttp.ClientTimeout(total=timeout_s),
            ) as response:
                if 200 <= response.status < 300:
                    logger.info(
                        "audio_input_hook: pushed %d frames (%d bytes) "
                        "session=%s status=%d",
                        len(frames), len(ogg_payload), session_id,
                        response.status,
                    )
                    return True
                body_snippet = (await response.text())[:200]
                logger.warning(
                    "audio_input_hook: POST returned status=%d session=%s "
                    "body=%r",
                    response.status, session_id, body_snippet,
                )
                return False
    except aiohttp.ClientError as exc:
        logger.warning(
            "audio_input_hook: POST failed (network error) session=%s: %s",
            session_id, exc,
        )
        return False
    except Exception as exc:
        logger.warning(
            "audio_input_hook: POST failed (unexpected) session=%s: %s",
            session_id, exc,
        )
        return False
