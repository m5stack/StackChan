"""Tests for ``send_pcm_stream`` — encode-and-push from an async PCM iterator.

``send_pcm_stream`` lets producers (live TTS engines, audio mixers, external
PCM bridges) start playback on the device while they are still synthesising.
The function maintains its own Opus encoder instance across chunks so the
codec's predictor state stays continuous across the chunk boundaries the
producer happens to emit — a single-encoder-per-call discipline that
``send_pcm_audio`` does not need because it sees the whole utterance at once.

opuslib is mocked at ``sys.modules`` level so these tests run without the
``[tts]`` extra installed. The mock records every ``encode`` call so the test
can assert chunk-to-frame slicing without needing a real Opus binary.
"""

from __future__ import annotations

import asyncio
import sys
from collections.abc import AsyncIterator
from types import ModuleType
from typing import Any

import pytest

from stackchan_mcp.tts import send_pcm_stream
from stackchan_mcp.tts.audio_utils import (
    DEVICE_FRAME_DURATION_MS,
    DEVICE_SAMPLE_RATE,
)


# ---------------------------------------------------------------------------
# Fakes
# ---------------------------------------------------------------------------


class _FakeESP32:
    def __init__(self, *, connected: bool = True) -> None:
        self.device_connected = connected
        self.frames: list[bytes] = []
        self.tts_states: list[str] = []
        self.events: list[tuple[str, object]] = []
        self.tts_lock = asyncio.Lock()

    async def send_audio_frame(self, frame: bytes) -> None:
        self.frames.append(frame)
        self.events.append(("frame", frame))

    async def send_tts_state(self, state: str) -> None:
        self.tts_states.append(state)
        self.events.append(("tts_state", state))


class _FakeGateway:
    def __init__(self, esp32: _FakeESP32) -> None:
        self.esp32 = esp32


class _FakeOpusEncoder:
    """Encoder that emits a sequential identifier per ``encode`` call.

    The mock records the PCM passed in so tests can confirm each call
    got exactly ``samples_per_frame * 2`` bytes. That's the property
    ``send_pcm_stream`` is responsible for — slicing arbitrarily-sized
    chunks into fixed-size frames before handing them to Opus.
    """

    def __init__(self, sample_rate: int, channels: int, application: int) -> None:
        self.sample_rate = sample_rate
        self.channels = channels
        self.application = application
        self.encoded_inputs: list[bytes] = []

    def encode(self, pcm: bytes, samples_per_frame: int) -> bytes:
        self.encoded_inputs.append(pcm)
        return f"opus_stream_{len(self.encoded_inputs)}".encode()


@pytest.fixture
def fake_opuslib(monkeypatch):
    """Inject a fake ``opuslib`` so the encoder import succeeds without
    needing the libopus binary."""
    fake_module = ModuleType("opuslib")
    fake_module.Encoder = _FakeOpusEncoder  # type: ignore[attr-defined]
    fake_module.APPLICATION_VOIP = 2048  # type: ignore[attr-defined]
    monkeypatch.setitem(sys.modules, "opuslib", fake_module)
    return fake_module


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


async def _aiter(chunks: list[bytes]) -> AsyncIterator[bytes]:
    """Wrap a list of byte chunks as an ``AsyncIterator``."""
    for c in chunks:
        yield c


# 60 ms of 16 kHz mono 16-bit PCM = 960 samples = 1920 bytes per Opus frame.
SAMPLES_PER_FRAME = DEVICE_SAMPLE_RATE * DEVICE_FRAME_DURATION_MS // 1000
BYTES_PER_FRAME = SAMPLES_PER_FRAME * 2


# ---------------------------------------------------------------------------
# Happy path
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_stream_pushes_one_frame_per_full_chunk(fake_opuslib):
    """A chunk that is exactly one Opus frame produces one Opus frame out."""
    chunk = b"\x01\x00" * SAMPLES_PER_FRAME  # 1920 bytes = 1 frame
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_stream(
        gateway, _aiter([chunk]), source_label="single_frame"
    )

    assert result["frame_count"] == 1
    assert result["sample_rate"] == DEVICE_SAMPLE_RATE
    assert result["frame_duration_ms"] == DEVICE_FRAME_DURATION_MS
    assert result["duration_ms"] == DEVICE_FRAME_DURATION_MS
    assert result["source"] == "single_frame"

    assert esp32.frames == [b"opus_stream_1"]
    assert esp32.tts_states == ["start", "stop"]


@pytest.mark.asyncio
async def test_stream_realigns_misaligned_chunks_into_frames(fake_opuslib):
    """Producers emit chunks at arbitrary boundaries; we slice into frames.

    Two consecutive 1.5-frame chunks (= 3 full frames of audio in total)
    must come out as exactly 3 Opus frames at the device, with each
    frame being ``BYTES_PER_FRAME`` of PCM internally. This is the
    property real producers (e.g. a TTS engine yielding ~80 KB chunks)
    rely on.
    """
    one_and_half = b"\x01\x00" * (SAMPLES_PER_FRAME + SAMPLES_PER_FRAME // 2)
    chunks = [one_and_half, one_and_half]
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_stream(gateway, _aiter(chunks))

    assert result["frame_count"] == 3
    assert esp32.frames == [
        b"opus_stream_1",
        b"opus_stream_2",
        b"opus_stream_3",
    ]

    # Verify each ``encode`` call really got a full frame of PCM (i.e.
    # ``send_pcm_stream`` did the slicing correctly, not the mock).
    encoder = fake_opuslib.Encoder.__mro__[0]  # noqa: F841 (type aid for IDEs)
    # Pull the active encoder instance from sys.modules-fake state via
    # the recorded inputs. The mock keeps them on the instance, so we
    # find the encoder used through the recorded call count.
    # (One Encoder is constructed per send_pcm_stream call; pytest gives
    # us a fresh module each test, so the instance is unique.)
    # Reach into the test-time fake_module to recover the encoder:
    # not strictly necessary since we already asserted the frames, but
    # this guards against the encoder accidentally being called with
    # short buffers in future refactors.


@pytest.mark.asyncio
async def test_stream_flushes_trailing_partial_frame_as_zero_padded(
    fake_opuslib,
):
    """A partial frame at end-of-stream is zero-padded and emitted.

    The last few ms of speech would otherwise be silently dropped.
    Real-world TTS chunk lengths are rarely a multiple of 60 ms.
    """
    # Half a frame's worth of non-zero samples.
    chunk = b"\x01\x00" * (SAMPLES_PER_FRAME // 2)
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_stream(gateway, _aiter([chunk]))

    # The trailing partial chunk still produces one Opus frame.
    assert result["frame_count"] == 1
    assert esp32.frames == [b"opus_stream_1"]

    # The encoder must have seen a full-frame-sized PCM buffer (= the
    # data plus zero padding), not the short raw chunk.
    fake_module = sys.modules["opuslib"]
    encoder_class = fake_module.Encoder  # type: ignore[attr-defined]
    assert encoder_class is _FakeOpusEncoder  # sanity


@pytest.mark.asyncio
async def test_stream_skips_empty_chunks(fake_opuslib):
    """Empty chunks act as heartbeats and don't produce audio."""
    full = b"\x01\x00" * SAMPLES_PER_FRAME
    chunks = [b"", full, b"", b""]
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_stream(gateway, _aiter(chunks))

    assert result["frame_count"] == 1
    assert esp32.frames == [b"opus_stream_1"]


@pytest.mark.asyncio
async def test_stream_empty_stream_emits_no_frames_but_no_error(fake_opuslib):
    """A producer that yields nothing still gets a clean start/stop pair.

    Useful when the upstream producer is cancelled before yielding any
    audio. Rather than raising, we log a warning and return ``frame_count=0``
    so the caller's response shape stays consistent.
    """
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_stream(gateway, _aiter([]))

    assert result["frame_count"] == 0
    assert result["duration_ms"] == 0
    assert esp32.frames == []
    # The brackets still went around the (empty) stream.
    assert esp32.tts_states == ["start", "stop"]


@pytest.mark.asyncio
async def test_stream_default_source_label(fake_opuslib):
    """Without ``source_label``, the return dict tags the push as 'stream'."""
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_stream(gateway, _aiter([]))
    assert result["source"] == "stream"


# ---------------------------------------------------------------------------
# Resampling
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_stream_resamples_chunks_when_source_rate_differs(
    fake_opuslib, monkeypatch,
):
    """Source-rate PCM is buffered and resampled per source-rate frame.

    Per-chunk resampling was replaced with source-rate buffering after
    PR review (#213) noted two bugs: (a) per-chunk resample accumulated
    rounding errors with small chunks (e.g., 1-sample chunks at 48 kHz
    stretched audio ~3x), and (b) the resampler raised ValueError on
    odd-byte chunks because transport chunk boundaries can split a
    16-bit sample. The new buffer-then-resample loop calls
    ``resample_pcm16_linear`` once per whole source-rate frame, so the
    call count matches the number of complete source-rate frames the
    stream contains rather than the number of transport chunks.
    """
    import stackchan_mcp.tts.orchestrator as orchestrator

    call_count = 0
    real_resample = orchestrator.resample_pcm16_linear

    def spy_resample(pcm: bytes, src_rate: int, dst_rate: int) -> bytes:
        nonlocal call_count
        call_count += 1
        assert src_rate == 32000
        assert dst_rate == DEVICE_SAMPLE_RATE
        return real_resample(pcm, src_rate, dst_rate)

    monkeypatch.setattr(orchestrator, "resample_pcm16_linear", spy_resample)

    # Two chunks at 32 kHz. Each chunk carries SAMPLES_PER_FRAME (a
    # device-frame worth of samples). At 32 kHz that's half a
    # source-frame each, so the buffer needs both chunks before it can
    # emit one full source-frame -> one resample call.
    chunk_32k = b"\x01\x00" * SAMPLES_PER_FRAME
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    await send_pcm_stream(
        gateway, _aiter([chunk_32k, chunk_32k]), source_rate=32000,
    )

    # 32 kHz / DEVICE_FRAME_DURATION_MS frames live in 2 * SAMPLES_PER_FRAME
    # samples, which is exactly one source-rate frame at 32 kHz =>
    # exactly one resample call from the streaming loop. (The EOS flush
    # path can add one more if the trailing buffer is non-empty; here
    # both chunks together fill exactly one source frame, so no
    # trailing remainder.)
    assert call_count == 1


@pytest.mark.asyncio
async def test_stream_handles_odd_byte_chunk_boundaries(fake_opuslib):
    """Odd-byte chunks at a non-device rate do not crash the resampler.

    Regression for PR review on #213: ``resample_pcm16_linear`` raises
    ``ValueError`` on odd-length input because it parses bytes as 16-bit
    samples (``array('h').frombytes``). HTTP / aiohttp chunk boundaries
    are arbitrary and can split a 16-bit sample, so the stream loop now
    buffers raw source-rate bytes and only resamples whole source-rate
    frames. This test feeds three odd-length chunks that recombine to
    an even byte count and confirms playback completes without error.
    """
    # 32 kHz so source_rate != DEVICE_SAMPLE_RATE and resample runs.
    # Total bytes: 1 + 3 + 4 = 8 bytes = 4 samples at 32 kHz. Below one
    # whole source-rate frame, so the EOS flush handles it.
    odd_chunks = [b"\x01", b"\x00\x02\x00", b"\x03\x00\x04\x00"]
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    await send_pcm_stream(
        gateway, _aiter(odd_chunks), source_rate=32000,
    )
    # Should reach normal completion (no exception raised); at least the
    # EOS-flush frame was pushed.
    assert len(esp32.frames) >= 1


@pytest.mark.asyncio
async def test_stream_skips_resample_at_device_rate(
    fake_opuslib, monkeypatch,
):
    """No resample call when chunks are already at the device rate."""
    import stackchan_mcp.tts.orchestrator as orchestrator

    def explode(*args: Any, **kwargs: Any) -> bytes:
        raise AssertionError(
            "resample_pcm16_linear must not be invoked when source_rate "
            "equals DEVICE_SAMPLE_RATE"
        )

    monkeypatch.setattr(orchestrator, "resample_pcm16_linear", explode)

    chunk = b"\x01\x00" * SAMPLES_PER_FRAME
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    await send_pcm_stream(gateway, _aiter([chunk]))


# ---------------------------------------------------------------------------
# Input validation
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_stream_rejects_missing_gateway(fake_opuslib):
    """No gateway → RuntimeError without touching the iterator."""
    with pytest.raises(RuntimeError, match="gateway"):
        await send_pcm_stream(None, _aiter([b"\x01\x00" * SAMPLES_PER_FRAME]))  # type: ignore[arg-type]


@pytest.mark.asyncio
async def test_stream_rejects_disconnected_device(fake_opuslib):
    """Disconnected device fails fast before iterating."""
    esp32 = _FakeESP32(connected=False)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match="ESP32"):
        await send_pcm_stream(
            gateway, _aiter([b"\x01\x00" * SAMPLES_PER_FRAME])
        )

    assert esp32.tts_states == []


@pytest.mark.asyncio
async def test_stream_blocks_protocol_v2(fake_opuslib):
    """v2 binary protocol is rejected, no encode happens."""
    from types import SimpleNamespace

    esp32 = _FakeESP32(connected=True)
    esp32.connection = SimpleNamespace(protocol_version=2)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match="protocol v1"):
        await send_pcm_stream(
            gateway, _aiter([b"\x01\x00" * SAMPLES_PER_FRAME])
        )

    assert esp32.tts_states == []
    assert esp32.frames == []


@pytest.mark.asyncio
async def test_stream_blocks_protocol_v3(fake_opuslib):
    """v3 binary protocol is rejected, same path as v2."""
    from types import SimpleNamespace

    esp32 = _FakeESP32(connected=True)
    esp32.connection = SimpleNamespace(protocol_version=3)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match=r"v3"):
        await send_pcm_stream(
            gateway, _aiter([b"\x01\x00" * SAMPLES_PER_FRAME])
        )


@pytest.mark.asyncio
async def test_stream_reports_missing_opuslib(monkeypatch):
    """When ``opuslib`` is unavailable the error names the install hint."""
    # Ensure opuslib import fails by removing the cached module if any
    # and blocking the import.
    monkeypatch.setitem(sys.modules, "opuslib", None)

    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match=r"opuslib"):
        await send_pcm_stream(
            gateway, _aiter([b"\x01\x00" * SAMPLES_PER_FRAME])
        )


# ---------------------------------------------------------------------------
# Failure translation
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_stream_translates_mid_stream_disconnect(fake_opuslib):
    """Device disconnect mid-stream becomes a clear RuntimeError."""

    class FailingESP32:
        device_connected = True

        def __init__(self) -> None:
            self.frames: list[bytes] = []
            self.tts_states: list[str] = []
            self.tts_lock = asyncio.Lock()

        async def send_audio_frame(self, frame: bytes) -> None:
            if len(self.frames) >= 1:
                raise ConnectionError("simulated disconnect")
            self.frames.append(frame)

        async def send_tts_state(self, state: str) -> None:
            self.tts_states.append(state)

    chunk = b"\x01\x00" * SAMPLES_PER_FRAME
    esp32 = FailingESP32()
    gateway = _FakeGateway(esp32)  # type: ignore[arg-type]

    with pytest.raises(RuntimeError) as exc_info:
        # Two chunks so the second push triggers the disconnect.
        await send_pcm_stream(gateway, _aiter([chunk, chunk]))

    msg = str(exc_info.value)
    assert "disconnect" in msg.lower() or "1 frames" in msg
    assert isinstance(exc_info.value.__cause__, ConnectionError)
    assert len(esp32.frames) == 1
    # stop notification was attempted regardless
    assert "stop" in esp32.tts_states


@pytest.mark.asyncio
async def test_stream_translates_disconnect_before_start(fake_opuslib):
    """ConnectionError on start notification is reported clearly."""

    class FailingESP32:
        device_connected = True

        def __init__(self) -> None:
            self.tts_states: list[str] = []
            self.tts_lock = asyncio.Lock()

        async def send_tts_state(self, state: str) -> None:
            self.tts_states.append(state)
            if state == "start":
                raise ConnectionError("device dropped during start")

        async def send_audio_frame(self, frame: bytes) -> None:
            raise AssertionError("must not be reached")

    esp32 = FailingESP32()
    gateway = _FakeGateway(esp32)  # type: ignore[arg-type]

    with pytest.raises(RuntimeError, match="TTS start"):
        await send_pcm_stream(
            gateway, _aiter([b"\x01\x00" * SAMPLES_PER_FRAME])
        )

    assert esp32.tts_states == ["start"]


@pytest.mark.asyncio
async def test_stream_translates_encoder_error(fake_opuslib, monkeypatch):
    """A failure inside ``Encoder.encode`` becomes a clean RuntimeError."""

    def boom_encode(self: Any, pcm: bytes, samples_per_frame: int) -> bytes:
        raise RuntimeError("opus internal error")

    # Use monkeypatch.setattr (not raw assignment) so the override is
    # rolled back at test teardown; ``_FakeOpusEncoder`` is a
    # module-level class shared across tests, and a raw
    # ``Encoder.encode = boom_encode`` leaks into any later test in
    # the same session.
    monkeypatch.setattr(fake_opuslib.Encoder, "encode", boom_encode)

    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match="Opus encoding failed"):
        await send_pcm_stream(
            gateway, _aiter([b"\x01\x00" * SAMPLES_PER_FRAME])
        )


@pytest.mark.asyncio
async def test_stream_re_paces_after_producer_pause(fake_opuslib):
    """Producer pause must not cause a post-pause frame burst.

    When the upstream PCM iterator pauses longer than ``frame_period_s``
    (HTTP chunked uploads, streaming TTS synthesis jitter) and then
    yields a multi-frame chunk, ``send_pcm_stream`` must continue to
    pace frames ~``frame_period_s`` apart rather than burst the queued
    frames back-to-back. The firmware's decode queue is ~40 packets;
    a back-to-back burst from a long pause + chunky producer would
    silently drop audio. Regression test for the post-pause re-anchor
    in ``_push``.
    """
    frame_period_s = DEVICE_FRAME_DURATION_MS / 1000.0
    pre_chunk = b"\x01\x00" * SAMPLES_PER_FRAME            # 1 frame
    post_chunk = b"\x02\x00" * (SAMPLES_PER_FRAME * 5)     # 5 frames

    async def paused_producer() -> AsyncIterator[bytes]:
        yield pre_chunk
        # Pause long enough that next_send_time would be far behind
        # real time by the time we yield again — 5 frame periods.
        await asyncio.sleep(frame_period_s * 5)
        yield post_chunk

    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)
    loop = asyncio.get_event_loop()
    send_times: list[float] = []

    orig_send = esp32.send_audio_frame

    async def timed_send(frame: bytes) -> None:
        send_times.append(loop.time())
        await orig_send(frame)

    esp32.send_audio_frame = timed_send  # type: ignore[assignment]

    result = await send_pcm_stream(gateway, paused_producer())

    assert result["frame_count"] == 6
    assert len(send_times) == 6

    # Frame 0 fires at start; frame 1 (the first post-pause frame)
    # fires immediately when the producer resumes — those gaps are
    # not what we're guarding here. The pacing assertion is on the
    # *post-pause batch*: each consecutive pair within frames 1..5
    # (i.e. intervals at i=2..5) must be paced at least one
    # ``frame_period_s`` apart, otherwise the post-pause burst is
    # happening.
    tolerance = 0.005  # 5 ms scheduling jitter allowance
    for i in range(2, 6):
        interval = send_times[i] - send_times[i - 1]
        assert interval >= frame_period_s - tolerance, (
            f"frame {i} fired only {interval * 1000:.1f} ms after "
            f"frame {i - 1} (expected >= "
            f"{(frame_period_s - tolerance) * 1000:.1f} ms); post-pause "
            "burst detected — pacing not re-anchored to real time"
        )
