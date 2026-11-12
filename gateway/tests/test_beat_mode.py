from __future__ import annotations

import asyncio
from types import SimpleNamespace
from typing import Any
import wave

import pytest
import pytest_asyncio

from stackchan_mcp.audio_stream import (
    handle_audio_frame,
    is_recording,
    recording_owner,
    stop_recording,
)
from stackchan_mcp.beat import mode as beat_mode
from stackchan_mcp.beat.mode import (
    BeatMode,
    BeatModeConfig,
    min_onset_rms_for_sensitivity,
)
from stackchan_mcp.stt import EngineRegistry, STTEngine, listen_and_transcribe
from stackchan_mcp.stt.audio_utils import DEVICE_SAMPLE_RATE


class _FakeESP32:
    def __init__(self) -> None:
        self.device_connected = True
        self.connection = SimpleNamespace(
            protocol_version=1,
            session_id="beat-session",
        )
        self.listen_lock = asyncio.Lock()
        self.listen_states: list[tuple[str, str | None]] = []
        self.listen_profiles: list[tuple[str, str | None]] = []
        self.tool_calls: list[tuple[str, dict[str, Any]]] = []

    async def send_listen_state(
        self,
        state: str,
        mode: str = "manual",
        profile: str = "voice",
    ) -> None:
        self.listen_states.append((state, mode if state == "start" else None))
        self.listen_profiles.append((state, profile if state == "start" else None))

    async def call_tool(
        self,
        name: str,
        arguments: dict[str, Any],
    ) -> tuple[dict[str, Any], None]:
        self.tool_calls.append((name, dict(arguments)))
        return {"ok": True}, None


class _FakeGateway:
    def __init__(self) -> None:
        self.esp32 = _FakeESP32()


class _NoopEngine(STTEngine):
    name = "faster-whisper"

    async def transcribe(self, pcm: bytes, **opts: Any) -> dict[str, Any]:
        return {"text": "", "language": opts.get("language") or "ja"}


@pytest.fixture
def fake_decode(monkeypatch: pytest.MonkeyPatch) -> list[Any]:
    instances: list[Any] = []

    class FakeStreamingOpusDecoder:
        def __init__(self, **kwargs: Any) -> None:
            self.kwargs = kwargs
            self.frames: list[bytes] = []
            instances.append(self)

        def decode_frame(self, frame: bytes) -> bytes:
            self.frames.append(frame)
            return frame

    monkeypatch.setattr(beat_mode, "StreamingOpusDecoder", FakeStreamingOpusDecoder)
    return instances


async def _wait_until(condition: Any, *, attempts: int = 50) -> None:
    for _ in range(attempts):
        if condition():
            return
        await asyncio.sleep(0)
    assert condition()


@pytest_asyncio.fixture(autouse=True)
async def _cleanup_beat_mode() -> Any:
    await beat_mode.stop_beat_mode()
    if is_recording():
        stop_recording()
    beat_mode._mode = None
    yield
    await beat_mode.stop_beat_mode()
    if is_recording():
        stop_recording()
    beat_mode._mode = None


@pytest.mark.asyncio
async def test_start_stop_are_idempotent_and_release_recording(fake_decode) -> None:
    gateway = _FakeGateway()

    started = await beat_mode.start_beat_mode(
        gateway,
        BeatModeConfig(motion_intensity=0.5),
    )
    started_again = await beat_mode.start_beat_mode(
        gateway,
        BeatModeConfig(motion_intensity=0.8),
    )

    assert started["active"] is True
    assert started_again["active"] is True
    assert started_again["motion"]["intensity"] == 0.8
    assert is_recording()
    assert beat_mode.is_beat_mode_owner(recording_owner())
    assert gateway.esp32.listen_states == [("start", "manual")]
    assert gateway.esp32.listen_profiles == [("start", "raw")]

    stopped = await beat_mode.stop_beat_mode()
    stopped_again = await beat_mode.stop_beat_mode()

    assert stopped["active"] is False
    assert stopped_again["active"] is False
    assert not is_recording()
    assert gateway.esp32.listen_states == [
        ("start", "manual"),
        ("stop", None),
    ]


def test_sensitivity_mapping_anchors() -> None:
    assert min_onset_rms_for_sensitivity(0.0) == pytest.approx(0.025)
    assert min_onset_rms_for_sensitivity(0.5) == pytest.approx(0.004)
    assert min_onset_rms_for_sensitivity(1.0) == pytest.approx(0.001)
    assert BeatModeConfig().min_onset_rms == pytest.approx(0.004)


@pytest.mark.asyncio
async def test_start_cancellation_releases_recording_slot_after_claim(
    fake_decode,
) -> None:
    gateway = _FakeGateway()
    send_started = asyncio.Event()
    release_send = asyncio.Event()

    async def blocked_send_listen_state(
        state: str,
        mode: str = "manual",
        profile: str = "voice",
    ) -> None:
        gateway.esp32.listen_states.append((state, mode if state == "start" else None))
        gateway.esp32.listen_profiles.append(
            (state, profile if state == "start" else None)
        )
        if state == "start":
            send_started.set()
            await release_send.wait()

    gateway.esp32.send_listen_state = blocked_send_listen_state  # type: ignore[method-assign]

    task = asyncio.create_task(beat_mode.start_beat_mode(gateway, BeatModeConfig()))
    await asyncio.wait_for(send_started.wait(), timeout=0.1)

    assert is_recording()
    assert beat_mode.is_beat_mode_owner(recording_owner())

    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task

    assert not is_recording()
    assert beat_mode._mode is None
    assert beat_mode.get_beat_mode_snapshot() == {"active": False}


@pytest.mark.asyncio
async def test_explicit_stop_waits_for_auto_stop_cleanup(fake_decode) -> None:
    gateway = _FakeGateway()
    stop_started = asyncio.Event()
    release_stop = asyncio.Event()

    async def blocked_send_listen_state(
        state: str,
        mode: str = "manual",
        profile: str = "voice",
    ) -> None:
        gateway.esp32.listen_states.append((state, mode if state == "start" else None))
        gateway.esp32.listen_profiles.append(
            (state, profile if state == "start" else None)
        )
        if state == "stop":
            stop_started.set()
            await release_stop.wait()

    gateway.esp32.send_listen_state = blocked_send_listen_state  # type: ignore[method-assign]

    await beat_mode.start_beat_mode(gateway, BeatModeConfig())
    mode = beat_mode._mode
    assert mode is not None

    auto_stop = asyncio.create_task(beat_mode._stop_mode_instance(mode))
    await asyncio.wait_for(stop_started.wait(), timeout=0.1)

    assert mode.active is True
    assert is_recording()

    explicit_stop = asyncio.create_task(beat_mode.stop_beat_mode())
    await asyncio.sleep(0)

    assert not explicit_stop.done()

    release_stop.set()
    auto_status = await auto_stop
    explicit_status = await explicit_stop

    assert auto_status["active"] is False
    assert explicit_status["active"] is False
    assert not is_recording()
    assert mode._tasks == []
    assert gateway.esp32.listen_states == [
        ("start", "manual"),
        ("stop", None),
    ]


@pytest.mark.asyncio
async def test_stale_cleanup_does_not_release_restarted_mode_slot(fake_decode) -> None:
    gateway = _FakeGateway()

    await beat_mode.start_beat_mode(gateway, BeatModeConfig())
    old_mode = beat_mode._mode
    old_owner = recording_owner()
    assert old_mode is not None
    assert old_owner is not None

    await beat_mode.stop_beat_mode()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())

    new_owner = recording_owner()
    assert new_owner is not None
    assert new_owner != old_owner
    assert beat_mode.is_beat_mode_owner(new_owner)

    old_mode._release_recording_slot()

    assert is_recording()
    assert recording_owner() == new_owner


@pytest.mark.asyncio
async def test_listen_fails_fast_while_beat_mode_owns_mic(fake_decode) -> None:
    gateway = _FakeGateway()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())

    reg = EngineRegistry()
    reg.register(_NoopEngine())

    with pytest.raises(RuntimeError, match="beat mode"):
        await listen_and_transcribe(
            {"duration_ms": 500},
            gateway=gateway,
            registry=reg,
        )

    assert gateway.esp32.listen_states == [("start", "manual")]


@pytest.mark.asyncio
async def test_beat_mode_does_not_hold_audio_lock_during_capture(fake_decode) -> None:
    gateway = _FakeGateway()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())

    await asyncio.wait_for(gateway.esp32.listen_lock.acquire(), timeout=0.1)
    gateway.esp32.listen_lock.release()


@pytest.mark.asyncio
async def test_clip_save_writes_recent_pcm_as_wav(fake_decode) -> None:
    gateway = _FakeGateway()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())

    pcm = b"\x01\x00" * int(0.25 * DEVICE_SAMPLE_RATE)
    await handle_audio_frame(pcm, session_id="beat-session")
    await _wait_until(
        lambda: beat_mode.get_beat_mode_snapshot()["frames_decoded"] >= 1
    )

    result = await beat_mode.save_beat_clip(1.0)

    with wave.open(result["path"], "rb") as wav:
        assert wav.getframerate() == DEVICE_SAMPLE_RATE
        assert wav.getnchannels() == 1
        assert wav.getsampwidth() == 2
        assert wav.getnframes() == int(0.25 * DEVICE_SAMPLE_RATE)

    assert result["seconds"] == 0.25
    assert result["active"] is True


@pytest.mark.asyncio
async def test_streaming_decoder_reused_across_frames_and_reset_between_sessions(
    fake_decode,
) -> None:
    gateway = _FakeGateway()

    await beat_mode.start_beat_mode(gateway, BeatModeConfig())
    first_frame = b"\x01\x00" * 10
    second_frame = b"\x02\x00" * 10
    await handle_audio_frame(first_frame, session_id="beat-session")
    await handle_audio_frame(second_frame, session_id="beat-session")
    await _wait_until(
        lambda: beat_mode.get_beat_mode_snapshot()["frames_decoded"] >= 2
    )

    assert len(fake_decode) == 1
    assert fake_decode[0].frames == [first_frame, second_frame]

    await beat_mode.stop_beat_mode()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())
    third_frame = b"\x03\x00" * 10
    await handle_audio_frame(third_frame, session_id="beat-session")
    await _wait_until(
        lambda: beat_mode.get_beat_mode_snapshot()["frames_decoded"] >= 1
    )

    assert len(fake_decode) == 2
    assert fake_decode[1].frames == [third_frame]
    assert fake_decode[0] is not fake_decode[1]


@pytest.mark.asyncio
async def test_reconnect_mid_mode_resets_decoder_and_drops_stale_frames(
    fake_decode,
) -> None:
    gateway = _FakeGateway()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())
    mode = beat_mode._mode
    assert mode is not None

    stale_frame = b"\x11\x00" * 10
    mode._enqueue_opus_frame(stale_frame)
    old_decoder = mode._decoder

    gateway.esp32.connection.session_id = "beat-session-reconnected"
    await mode._arm_listen_unlocked()

    assert gateway.esp32.listen_profiles == [
        ("start", "raw"),
        ("start", "raw"),
    ]
    assert mode._session_id == "beat-session-reconnected"
    assert len(fake_decode) == 2
    assert old_decoder is fake_decode[0]
    assert mode._decoder is fake_decode[1]
    assert mode._opus_queue.empty()
    assert mode.status()["frames_dropped"] == 1

    await handle_audio_frame(b"\x12\x00" * 10, session_id="beat-session")
    new_frame = b"\x13\x00" * 10
    await handle_audio_frame(new_frame, session_id="beat-session-reconnected")
    await _wait_until(
        lambda: beat_mode.get_beat_mode_snapshot()["frames_decoded"] >= 1
    )

    assert fake_decode[0].frames == []
    assert fake_decode[1].frames == [new_frame]


def test_watchdog_uses_newest_listen_or_audio_timestamp() -> None:
    mode = BeatMode(_FakeGateway(), BeatModeConfig())
    restart_after = beat_mode.LISTEN_RESTART_AFTER_S

    class FakeClock:
        now = 0.0

    clock = FakeClock()

    # Silence: initial listen.start is the only reference point.
    mode._last_audio_at = None
    mode._last_listen_start_at = 10.0
    clock.now = 10.0 + restart_after - 0.001
    assert mode._listen_restart_due(clock.now) is False
    clock.now = 10.0 + restart_after
    assert mode._listen_restart_due(clock.now) is True

    # say() interruption: a fresh re-arm must win over stale audio.
    mode._last_audio_at = 20.0
    mode._last_listen_start_at = 30.0
    clock.now = 30.0 + restart_after - 0.001
    assert mode._listen_restart_due(clock.now) is False
    clock.now = 30.0 + restart_after
    assert mode._listen_restart_due(clock.now) is True

    # Reconnection: the new listen.start timestamp resets the watchdog.
    mode._last_audio_at = 40.0
    mode._last_listen_start_at = 45.0
    clock.now = 45.0 + restart_after - 0.001
    assert mode._listen_restart_due(clock.now) is False
    clock.now = 45.0 + restart_after
    assert mode._listen_restart_due(clock.now) is True


@pytest.mark.asyncio
async def test_update_applies_runtime_parameters(fake_decode) -> None:
    gateway = _FakeGateway()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())

    status = await beat_mode.update_beat_mode(
        motion_intensity=0.2,
        sensitivity=0.0,
        color=(255, 64, 0),
        blink_rate=2.0,
        motion_enabled=False,
        led_enabled=False,
    )

    assert status["motion"]["intensity"] == 0.2
    assert status["sensitivity"] == 0.0
    assert status["min_onset_rms"] == pytest.approx(0.025)
    assert status["motion"]["enabled"] is False
    assert status["led"]["color"] == [255, 64, 0]
    assert status["led"]["blink_rate"] == 2.0
    assert status["led"]["enabled"] is False


@pytest.mark.asyncio
async def test_update_sensitivity_preserves_tracker_history(fake_decode) -> None:
    gateway = _FakeGateway()
    await beat_mode.start_beat_mode(gateway, BeatModeConfig())
    mode = beat_mode._mode
    assert mode is not None

    with mode._tracker._lock:
        mode._tracker._record_onset(1.0)
        mode._tracker._record_onset(1.5)
        mode._tracker._record_onset(2.0)
        mode._tracker._update_bpm_locked(2.0)
    before = mode._tracker.snapshot()

    status = await beat_mode.update_beat_mode(sensitivity=1.0)

    assert mode._tracker.snapshot() == before
    assert status["onsets"] == before.onset_count
    assert status["bpm"] == before.bpm
    assert status["sensitivity"] == 1.0
    assert status["min_onset_rms"] == pytest.approx(0.001)


def test_config_validation_rejects_invalid_values() -> None:
    with pytest.raises(ValueError, match="motion_intensity"):
        BeatModeConfig(motion_intensity=1.5)
    with pytest.raises(ValueError, match="sensitivity"):
        BeatModeConfig(sensitivity=1.5)
    with pytest.raises(ValueError, match="color channels"):
        BeatModeConfig(color=(256, 0, 0))
    with pytest.raises(ValueError, match="blink_rate"):
        BeatModeConfig(blink_rate=0.1)
