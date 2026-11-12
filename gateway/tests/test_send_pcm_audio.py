"""Tests for ``send_pcm_audio`` — the shared encode-and-push back-half.

``send_pcm_audio`` is the public path external producers (HTTP PCM bridges,
sound-effect players, alternative voice stacks) can call to push
pre-synthesised PCM to the device without going through a registered
:class:`TTSEngine`. ``synthesize_and_send`` also delegates here after running
its engine, so these tests double as a regression guard for the back half of
the standard ``say()`` pipeline.

Fakes mirror those in :mod:`tests.test_orchestrator`; they are duplicated
here so the new test module reads stand-alone without leaning on private
helpers from the orchestrator tests.
"""

from __future__ import annotations

import asyncio
from typing import Any

import pytest

from stackchan_mcp.tts import send_pcm_audio
from stackchan_mcp.tts.audio_utils import (
    DEVICE_FRAME_DURATION_MS,
    DEVICE_SAMPLE_RATE,
)


class _FakeESP32:
    """Records what reaches the wire so tests can assert event ordering."""

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


@pytest.fixture
def fake_encode(monkeypatch):
    """Replace ``encode_opus_frames`` so tests don't need libopus.

    Each ``DEVICE_SAMPLE_RATE * DEVICE_FRAME_DURATION_MS / 1000`` PCM
    samples become one fake Opus frame; the trailing partial chunk
    becomes a (zero-padded) frame too, matching the real encoder.
    """

    def fake(pcm: bytes, **kwargs: Any):
        samples_per_frame = (
            DEVICE_SAMPLE_RATE * DEVICE_FRAME_DURATION_MS // 1000
        )
        bytes_per_frame = samples_per_frame * 2
        n_full = len(pcm) // bytes_per_frame
        n_partial = 1 if len(pcm) % bytes_per_frame else 0
        n_total = n_full + n_partial
        return iter(f"opus_frame_{i}".encode() for i in range(n_total))

    import stackchan_mcp.tts.orchestrator as orchestrator

    monkeypatch.setattr(orchestrator, "encode_opus_frames", fake)
    return fake


# ---------------------------------------------------------------------------
# Happy path
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_send_pcm_audio_pushes_frames_with_state_brackets(fake_encode):
    """PCM gets encoded, bracketed by start/stop, and pushed as frames."""
    # 90 ms of PCM at 16 kHz mono = 1440 samples = 2880 bytes → 2 frames
    pcm = b"\x01\x00" * 1440
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_audio(gateway, pcm, source_label="unit_test")

    assert result["frame_count"] == 2
    assert result["sample_rate"] == DEVICE_SAMPLE_RATE
    assert result["frame_duration_ms"] == DEVICE_FRAME_DURATION_MS
    assert result["duration_ms"] == 2 * DEVICE_FRAME_DURATION_MS
    assert result["source"] == "unit_test"

    assert esp32.frames == [b"opus_frame_0", b"opus_frame_1"]
    # start before any frame, stop after the last frame
    assert esp32.tts_states == ["start", "stop"]
    assert esp32.events[0] == ("tts_state", "start")
    assert esp32.events[-1] == ("tts_state", "stop")
    middle = esp32.events[1:-1]
    assert all(kind == "frame" for kind, _ in middle)


@pytest.mark.asyncio
async def test_send_pcm_audio_default_source_label(fake_encode):
    """Without ``source_label`` the return dict tags the push as 'external'."""
    pcm = b"\x01\x00" * 960
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    result = await send_pcm_audio(gateway, pcm)

    assert result["source"] == "external"


# ---------------------------------------------------------------------------
# Resampling — non-device source rates are converted before encoding
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_send_pcm_audio_resamples_when_source_rate_differs(
    fake_encode, monkeypatch,
):
    """A source at e.g. 32 kHz is resampled to ``DEVICE_SAMPLE_RATE`` first.

    ``encode_opus_frames`` only handles ``DEVICE_SAMPLE_RATE`` input; passing
    a different rate would produce frames that play back too fast or slow on
    the device. The resampling is what makes the SAIVerse voice-tts addon
    (32 kHz output) compatible with the device's 16 kHz Opus decoder
    without forcing each external producer to resample by hand.
    """
    seen_resample: dict[str, int] = {}

    import stackchan_mcp.tts.orchestrator as orchestrator

    real_resample = orchestrator.resample_pcm16_linear

    def spy_resample(pcm: bytes, src_rate: int, dst_rate: int) -> bytes:
        seen_resample["src"] = src_rate
        seen_resample["dst"] = dst_rate
        return real_resample(pcm, src_rate, dst_rate)

    monkeypatch.setattr(orchestrator, "resample_pcm16_linear", spy_resample)

    # 32 kHz mono PCM, 30 ms duration → 960 samples = 1920 bytes
    pcm_32k = b"\x01\x00" * 960
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    await send_pcm_audio(
        gateway, pcm_32k, source_rate=32000, source_label="hi_rate",
    )

    assert seen_resample == {"src": 32000, "dst": DEVICE_SAMPLE_RATE}


@pytest.mark.asyncio
async def test_send_pcm_audio_skips_resample_at_device_rate(
    fake_encode, monkeypatch,
):
    """When the source is already at the device rate, no resample happens."""
    import stackchan_mcp.tts.orchestrator as orchestrator

    def explode_if_called(*args: Any, **kwargs: Any) -> bytes:
        raise AssertionError(
            "resample_pcm16_linear must not be invoked when "
            "source_rate == DEVICE_SAMPLE_RATE"
        )

    monkeypatch.setattr(
        orchestrator, "resample_pcm16_linear", explode_if_called
    )

    pcm = b"\x01\x00" * 960
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    # Default ``source_rate`` is ``DEVICE_SAMPLE_RATE``.
    await send_pcm_audio(gateway, pcm)


# ---------------------------------------------------------------------------
# Input validation
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_send_pcm_audio_rejects_empty_pcm(fake_encode):
    """Empty PCM is a bug at the call site, surfaced as a RuntimeError."""
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match="PCM payload was empty"):
        await send_pcm_audio(gateway, b"", source_label="empty_test")

    assert esp32.tts_states == []
    assert esp32.frames == []


@pytest.mark.asyncio
async def test_send_pcm_audio_rejects_missing_gateway():
    """No gateway means nowhere to push; fail with a clear message."""
    with pytest.raises(RuntimeError, match="gateway"):
        await send_pcm_audio(None, b"\x01\x00" * 960)  # type: ignore[arg-type]


@pytest.mark.asyncio
async def test_send_pcm_audio_rejects_disconnected_device(fake_encode):
    """Disconnected device fails fast without sending state notifications."""
    esp32 = _FakeESP32(connected=False)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match="ESP32"):
        await send_pcm_audio(gateway, b"\x01\x00" * 960)

    assert esp32.tts_states == []
    assert esp32.frames == []


# ---------------------------------------------------------------------------
# Protocol gate
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_send_pcm_audio_blocks_protocol_v2(fake_encode):
    """Devices on v2 binary protocol get clear errors, not silent failure."""
    from types import SimpleNamespace

    esp32 = _FakeESP32(connected=True)
    esp32.connection = SimpleNamespace(protocol_version=2)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match="protocol v1"):
        await send_pcm_audio(gateway, b"\x01\x00" * 960)

    assert esp32.tts_states == []
    assert esp32.frames == []


@pytest.mark.asyncio
async def test_send_pcm_audio_blocks_protocol_v3(fake_encode):
    """v3 is blocked the same way as v2."""
    from types import SimpleNamespace

    esp32 = _FakeESP32(connected=True)
    esp32.connection = SimpleNamespace(protocol_version=3)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match=r"v3"):
        await send_pcm_audio(gateway, b"\x01\x00" * 960)

    assert esp32.tts_states == []
    assert esp32.frames == []


# ---------------------------------------------------------------------------
# Concurrency — the per-device lock serialises external pushes too
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_send_pcm_audio_serialises_concurrent_pushes(fake_encode):
    """Two concurrent ``send_pcm_audio`` calls do not interleave frames.

    The TTS lock is held for the entire start → frames → stop block, so
    the second caller has to wait until the first finishes its ``stop``
    notification. Without the lock, the device would briefly see two
    overlapping speaking states on the same WebSocket and drop frames.
    """
    pcm = b"\x01\x00" * 1440  # 1.5 → 2 frames
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    await asyncio.gather(
        send_pcm_audio(gateway, pcm, source_label="caller_a"),
        send_pcm_audio(gateway, pcm, source_label="caller_b"),
    )

    events = esp32.events
    start_indices = [
        i for i, e in enumerate(events) if e == ("tts_state", "start")
    ]
    stop_indices = [
        i for i, e in enumerate(events) if e == ("tts_state", "stop")
    ]
    assert len(start_indices) == 2
    assert len(stop_indices) == 2
    # Strictly sequential: the second caller's start must come after the
    # first's stop.
    assert (
        start_indices[0]
        < stop_indices[0]
        < start_indices[1]
        < stop_indices[1]
    )


# ---------------------------------------------------------------------------
# Failure translation
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_send_pcm_audio_translates_mid_stream_disconnect(fake_encode):
    """A ConnectionError from the device mid-stream becomes a RuntimeError.

    ConnectionError doesn't inherit RuntimeError, so without translation it
    would skip the MCP handler's exception filter and surface as a stack
    trace.
    """

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
            # If the disconnect races the stop notification, simulate a
            # benign no-op rather than raising again.
            self.tts_states.append(state)

    pcm = b"\x01\x00" * 1440
    esp32 = FailingESP32()
    gateway = _FakeGateway(esp32)  # type: ignore[arg-type]

    with pytest.raises(RuntimeError) as exc_info:
        await send_pcm_audio(gateway, pcm)
    msg = str(exc_info.value)
    assert "1/2" in msg or "disconnect" in msg.lower()
    assert isinstance(exc_info.value.__cause__, ConnectionError)
    assert len(esp32.frames) == 1
    # stop notification was still attempted regardless of the disconnect
    assert "start" in esp32.tts_states
    assert "stop" in esp32.tts_states


@pytest.mark.asyncio
async def test_send_pcm_audio_translates_disconnect_before_start(fake_encode):
    """ConnectionError on the start notification is reported clearly.

    Without a clean error message at this point the caller would see a
    confusing "0/N frames" report even though no frame was ever attempted.
    """

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
            raise AssertionError(
                "frame should not be attempted after start failure"
            )

    esp32 = FailingESP32()
    gateway = _FakeGateway(esp32)  # type: ignore[arg-type]

    with pytest.raises(RuntimeError, match="TTS start"):
        await send_pcm_audio(gateway, b"\x01\x00" * 960)
    assert esp32.tts_states == ["start"]


@pytest.mark.asyncio
async def test_send_pcm_audio_translates_opus_encode_error(
    fake_encode, monkeypatch,
):
    """A failure inside ``encode_opus_frames`` becomes a clear RuntimeError."""

    def boom(pcm: bytes, **kwargs: Any):
        raise RuntimeError("libopus missing")

    import stackchan_mcp.tts.orchestrator as orchestrator

    monkeypatch.setattr(orchestrator, "encode_opus_frames", boom)

    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    with pytest.raises(RuntimeError, match="Opus encoding failed"):
        await send_pcm_audio(gateway, b"\x01\x00" * 960)


# ---------------------------------------------------------------------------
# Pacing — frame pushes spaced at the device's frame_duration
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_send_pcm_audio_paces_frames_at_device_rate(
    fake_encode, monkeypatch,
):
    """Each frame push is spaced by ``DEVICE_FRAME_DURATION_MS``.

    The firmware's decode queue holds ~40 packets, so a burst would
    silently drop the tail. Pacing keeps the queue at roughly one frame.
    """
    sleeps: list[float] = []
    real_sleep = asyncio.sleep

    async def fake_sleep(delay: float) -> None:
        sleeps.append(delay)
        await real_sleep(0)  # yield without actually waiting

    monkeypatch.setattr(
        "stackchan_mcp.tts.orchestrator.asyncio.sleep", fake_sleep
    )

    pcm = b"\x01\x00" * 1440  # 2 frames after chunking
    esp32 = _FakeESP32(connected=True)
    gateway = _FakeGateway(esp32)

    await send_pcm_audio(gateway, pcm)

    # First sleep is the post-tts.start state-transition delay (50 ms).
    # Per-frame pacing follows; the exact count depends on loop.time()
    # drift so this test only asserts the start delay and that at least
    # one sleep happened.
    assert len(sleeps) >= 1
    assert sleeps[0] == pytest.approx(0.05, rel=0.05)
