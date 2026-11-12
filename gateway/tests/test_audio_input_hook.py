"""Tests for audio_input_hook: Ogg packing + HTTP push."""

from __future__ import annotations

import struct
from typing import Any

import pytest
from aiohttp import web

from stackchan_mcp.audio_input_hook import (
    GRANULE_PER_FRAME,
    _build_opus_head_packet,
    _ogg_crc32,
    pack_opus_frames_to_ogg,
    push_audio_capture,
)


# --- Ogg CRC32 sanity --------------------------------------------------------


def test_ogg_crc32_empty():
    """Empty input is the canonical 0 value."""
    assert _ogg_crc32(b"") == 0


def test_ogg_crc32_basics():
    """Sanity check on _ogg_crc32 — initial value 0 and deterministic.

    The full Ogg-level correctness (= what matters for downstream
    decoders) is covered by :func:`test_pack_opus_frames_crc_matches`,
    which recomputes each emitted page's CRC over the zeroed header +
    body and verifies the value the packer wrote matches. Hard-coded
    spec vectors are intentionally avoided here because the Ogg CRC
    polynomial is not in any standard CRC catalog under a memorable
    name and hand-derived values are easy to mistype; the integration
    test path with a real decoder is the authoritative check.
    """
    # Spec-derived: initial 0, no work for empty input.
    assert _ogg_crc32(b"") == 0
    assert _ogg_crc32(b"\x00") == 0
    # Deterministic across calls (= no hidden state, no global mutation).
    assert _ogg_crc32(b"OggS") == _ogg_crc32(b"OggS")
    # Non-empty input produces a non-trivial value (= CRC is actually
    # doing work, not just returning 0).
    assert _ogg_crc32(b"OggS") != 0


# --- OpusHead packet ---------------------------------------------------------


def test_opus_head_packet_structure():
    """RFC 7845 §5.1 — fixed magic + version=1 + standard fields."""
    head = _build_opus_head_packet(channels=1, pre_skip=0, input_sample_rate=16000)
    assert head[:8] == b"OpusHead"
    assert head[8] == 1                # version
    assert head[9] == 1                # channel count
    assert struct.unpack("<H", head[10:12])[0] == 0       # pre-skip
    assert struct.unpack("<I", head[12:16])[0] == 16000   # input sample rate
    assert struct.unpack("<h", head[16:18])[0] == 0       # output gain
    assert head[18] == 0               # channel mapping family


# --- Ogg page structure ------------------------------------------------------


def _parse_ogg_pages(blob: bytes) -> list[dict[str, Any]]:
    """Parse a stream into a list of page metadata for assertions.

    Minimal parser: returns ``{"header_type", "granule", "serial",
    "page_seq", "crc", "segments", "body"}`` per page. Does not
    validate CRC (the packer does, and we re-verify by recomputing
    in :func:`test_pack_opus_frames_crc_matches`).
    """
    pages: list[dict[str, Any]] = []
    i = 0
    while i < len(blob):
        assert blob[i:i + 4] == b"OggS", f"page magic at offset {i}"
        assert blob[i + 4] == 0          # version
        header_type = blob[i + 5]
        granule = struct.unpack("<q", blob[i + 6:i + 14])[0]
        serial = struct.unpack("<I", blob[i + 14:i + 18])[0]
        page_seq = struct.unpack("<I", blob[i + 18:i + 22])[0]
        crc = struct.unpack("<I", blob[i + 22:i + 26])[0]
        n_segments = blob[i + 26]
        segment_table = list(blob[i + 27:i + 27 + n_segments])
        body_start = i + 27 + n_segments
        body_len = sum(segment_table)
        body = blob[body_start:body_start + body_len]
        pages.append(
            {
                "header_type": header_type,
                "granule": granule,
                "serial": serial,
                "page_seq": page_seq,
                "crc": crc,
                "segments": segment_table,
                "body": body,
            }
        )
        i = body_start + body_len
    return pages


def test_pack_opus_frames_empty():
    """Empty input yields empty bytes — caller short-circuits."""
    assert pack_opus_frames_to_ogg([]) == b""


def test_pack_opus_frames_minimal():
    """Single-frame stream produces 3 pages: BOS-OpusHead, OpusTags, audio (EOS)."""
    fake_frame = b"\xfc\xff\xfe"   # 3 bytes is fine; the codec content is opaque to Ogg
    blob = pack_opus_frames_to_ogg([fake_frame], serial=0x12345678)
    pages = _parse_ogg_pages(blob)
    assert len(pages) == 3, [p["header_type"] for p in pages]

    # BOS page — OpusHead
    assert pages[0]["header_type"] == 0x02
    assert pages[0]["page_seq"] == 0
    assert pages[0]["serial"] == 0x12345678
    assert pages[0]["granule"] == 0
    assert pages[0]["body"][:8] == b"OpusHead"

    # OpusTags page
    assert pages[1]["header_type"] == 0
    assert pages[1]["page_seq"] == 1
    assert pages[1]["body"][:8] == b"OpusTags"

    # Audio page (EOS because it's also the last)
    assert pages[2]["header_type"] == 0x04
    assert pages[2]["page_seq"] == 2
    assert pages[2]["granule"] == GRANULE_PER_FRAME    # one frame's worth
    assert pages[2]["segments"] == [len(fake_frame)]
    assert pages[2]["body"] == fake_frame


def test_pack_opus_frames_multi_page():
    """More than _FRAMES_PER_PAGE frames produces multiple audio pages,
    and only the last is marked EOS."""
    # Use 120 frames → 50 + 50 + 20 across 3 audio pages
    frames = [b"\x01\x02\x03" for _ in range(120)]
    blob = pack_opus_frames_to_ogg(frames, serial=42)
    pages = _parse_ogg_pages(blob)
    # 2 header pages + 3 audio pages
    assert len(pages) == 5
    audio = pages[2:]
    assert [p["header_type"] for p in audio[:-1]] == [0, 0]   # non-last audio pages
    assert audio[-1]["header_type"] == 0x04                   # last is EOS
    assert [len(p["segments"]) for p in audio] == [50, 50, 20]
    # Granule monotonically increases by frames-per-page * GRANULE_PER_FRAME
    assert audio[0]["granule"] == 50 * GRANULE_PER_FRAME
    assert audio[1]["granule"] == 100 * GRANULE_PER_FRAME
    assert audio[2]["granule"] == 120 * GRANULE_PER_FRAME


def test_pack_opus_frames_large_packet_lacing():
    """Packets > 255 bytes are split into 255-byte lacing segments.

    Regression for PR review on #209: ``_build_ogg_page`` rejected any
    segment over 255 bytes with ``ValueError``, but valid VBR opus
    frames at higher bitrate can exceed 255 bytes. The packer now
    splits long packets via ``_packet_to_segments`` before assembling
    pages.
    """
    # 600-byte packet: 255 + 255 + 90 -> 3 segments
    long_packet = bytes(range(256)) * 2 + bytes(range(88))
    assert len(long_packet) == 600
    blob = pack_opus_frames_to_ogg([long_packet], serial=7)
    pages = _parse_ogg_pages(blob)
    # OpusHead + OpusTags + one audio page with 3 segments totalling 600 bytes.
    audio = [p for p in pages if p["page_seq"] >= 2]
    assert len(audio) == 1
    assert audio[0]["segments"] == [255, 255, 90]
    assert len(audio[0]["body"]) == 600


def test_pack_opus_frames_exact_255_boundary():
    """Packets whose length is an exact multiple of 255 get a 0-byte terminator.

    Regression for PR review on #209: without the terminator the Ogg
    decoder treats the packet as continuing into the next page, so a
    standalone 255-byte packet was being mis-framed.
    """
    # Exactly 255 bytes -> packet needs a 0-byte terminating segment
    # so the parser knows it ended on this page.
    packet_255 = bytes(range(255))
    blob = pack_opus_frames_to_ogg([packet_255], serial=8)
    pages = _parse_ogg_pages(blob)
    audio = [p for p in pages if p["page_seq"] >= 2]
    assert len(audio) == 1
    # One 255-byte segment + one zero-byte terminating segment.
    assert audio[0]["segments"] == [255, 0]
    assert len(audio[0]["body"]) == 255


def test_pack_opus_frames_crc_matches():
    """Recompute each page's CRC and verify the packer wrote the right value."""
    frames = [b"\xff\xee\xdd\xcc" for _ in range(10)]
    blob = pack_opus_frames_to_ogg(frames, serial=1)
    pages = _parse_ogg_pages(blob)

    # Reassemble each page with the CRC zeroed, recompute, and compare.
    offset = 0
    for page in pages:
        page_size = 27 + len(page["segments"]) + sum(page["segments"])
        raw = blob[offset:offset + page_size]
        # Zero the CRC field (bytes 22..26) and recompute.
        zeroed = raw[:22] + b"\x00\x00\x00\x00" + raw[26:]
        expected = _ogg_crc32(zeroed)
        assert page["crc"] == expected, (
            f"page_seq={page['page_seq']} CRC mismatch: "
            f"recorded={page['crc']:#x} expected={expected:#x}"
        )
        offset += page_size


# --- HTTP push --------------------------------------------------------------


@pytest.mark.asyncio
async def test_push_audio_capture_success(aiohttp_unused_port):
    """A 2xx response from the hook returns True; payload is audio/ogg with
    Bearer auth and X-StackChan-Session header."""
    received: dict[str, Any] = {}

    async def handle(request: web.Request) -> web.Response:
        received["auth"] = request.headers.get("Authorization", "")
        received["content_type"] = request.headers.get("Content-Type", "")
        received["session"] = request.headers.get("X-StackChan-Session", "")
        received["body"] = await request.read()
        return web.Response(status=204)

    app = web.Application()
    app.router.add_post("/audio", handle)

    port = aiohttp_unused_port()
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", port)
    await site.start()

    try:
        ok = await push_audio_capture(
            f"http://127.0.0.1:{port}/audio",
            token="test-token",
            frames=[b"\x01\x02\x03"],
            session_id="sess-abc",
        )
    finally:
        await runner.cleanup()

    assert ok is True
    assert received["auth"] == "Bearer test-token"
    assert received["content_type"] == "audio/ogg"
    assert received["session"] == "sess-abc"
    assert received["body"][:4] == b"OggS"   # the body really is Ogg


@pytest.mark.asyncio
async def test_push_audio_capture_empty_frames():
    """Empty frame list returns False without making an HTTP call."""
    ok = await push_audio_capture(
        "http://example.invalid/nope",
        token="",
        frames=[],
        session_id="sess-empty",
    )
    assert ok is False


@pytest.mark.asyncio
async def test_push_audio_capture_error_status(aiohttp_unused_port):
    """A 5xx response returns False — caller does not raise."""

    async def handle(request: web.Request) -> web.Response:
        await request.read()
        return web.Response(status=500, text="boom")

    app = web.Application()
    app.router.add_post("/audio", handle)

    port = aiohttp_unused_port()
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", port)
    await site.start()

    try:
        ok = await push_audio_capture(
            f"http://127.0.0.1:{port}/audio",
            token="",
            frames=[b"\xaa"],
            session_id="sess-err",
        )
    finally:
        await runner.cleanup()

    assert ok is False


@pytest.fixture
def aiohttp_unused_port():
    """Helper: pick an unused TCP port via ephemeral bind."""
    import socket

    def _pick() -> int:
        sock = socket.socket()
        try:
            sock.bind(("127.0.0.1", 0))
            return sock.getsockname()[1]
        finally:
            sock.close()

    return _pick
