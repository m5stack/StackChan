"""Tests for the STT orchestrator pipeline (Issue #91).

Symmetric to :mod:`tests.test_orchestrator` (the TTS counterpart).
Focuses on the pipeline shape — argument validation, listen-state
notifications, protocol-v1 gate, listen_lock serialisation, empty
captures, and clean error translation — without depending on the
heavy ML engines or libopus.
"""

from __future__ import annotations

import asyncio
from types import SimpleNamespace
from typing import Any

import pytest

import stackchan_mcp.stt.orchestrator as orchestrator
from stackchan_mcp.audio_stream import is_recording, start_recording, stop_recording
from stackchan_mcp.stt import EngineRegistry, STTEngine, listen_and_transcribe
from stackchan_mcp.stt.audio_utils import DEVICE_FRAME_DURATION_MS, DEVICE_SAMPLE_RATE


class _CapturingEngine(STTEngine):
    """Engine that returns fixed text and records what it received."""

    def __init__(self, text: str = "こんにちは", name: str = "faster-whisper") -> None:
        self.name = name
        self._text = text
        self.calls: list[tuple[bytes, dict[str, Any]]] = []

    async def transcribe(self, pcm: bytes, **opts: Any) -> dict[str, Any]:
        self.calls.append((pcm, dict(opts)))
        return {"text": self._text, "language": opts.get("language") or "ja"}


class _RaisingEngine(STTEngine):
    """Engine that always raises a configured exception."""

    def __init__(self, exc: Exception, name: str = "faster-whisper") -> None:
        self.name = name
        self._exc = exc

    async def transcribe(self, pcm: bytes, **opts: Any) -> dict[str, Any]:
        raise self._exc


class _FakeESP32:
    def __init__(
        self,
        *,
        connected: bool = True,
        protocol_version: int = 1,
        frames_to_inject: list[bytes] | None = None,
        injection_delay_s: float = 0.0,
    ) -> None:
        self.device_connected = connected
        self.connection = SimpleNamespace(
            protocol_version=protocol_version,
            session_id="session-test",
        )
        self.listen_states: list[tuple[str, str | None]] = []
        self.tool_calls: list[tuple[str, dict[str, Any]]] = []
        self.events: list[tuple[str, Any]] = []
        self.listen_lock = asyncio.Lock()
        self.head_yaw = 12.0
        self.head_pitch = 24.0
        self._frames_to_inject = list(frames_to_inject or [])
        self._injection_delay_s = injection_delay_s

    async def send_listen_state(self, state: str, mode: str = "manual") -> None:
        self.listen_states.append((state, mode if state == "start" else None))
        self.events.append(("listen_state", state))
        if state == "start" and self._frames_to_inject:
            # Schedule frame injection while the orchestrator is in the
            # capture window; we deliberately use create_task so the
            # injection runs concurrently with the orchestrator's
            # asyncio.sleep(duration_ms).
            asyncio.create_task(self._inject_frames())

    async def _inject_frames(self) -> None:
        # Delay slightly so the orchestrator has had time to mark
        # recording active. The transition-delay sleep in the
        # orchestrator (50 ms) is plenty in practice; we yield once
        # here to keep tests deterministic regardless of scheduling.
        await asyncio.sleep(self._injection_delay_s)
        from stackchan_mcp.audio_stream import handle_audio_frame

        for frame in self._frames_to_inject:
            await handle_audio_frame(frame, "session-test")

    async def call_tool(
        self, name: str, arguments: dict[str, Any]
    ) -> tuple[dict[str, Any], None]:
        self.tool_calls.append((name, dict(arguments)))
        self.events.append(("tool", name))
        if name == "self.robot.get_head_angles":
            return {"yaw": self.head_yaw, "pitch": self.head_pitch}, None
        if name == "self.robot.set_head_angles":
            self.head_yaw = float(arguments["yaw"])
            self.head_pitch = float(arguments["pitch"])
            return {"ok": True}, None
        if name == "self.display.set_avatar":
            return {"ok": True}, None
        raise AssertionError(f"unexpected tool call: {name}")


class _FakeGateway:
    def __init__(self, esp32: _FakeESP32) -> None:
        self.esp32 = esp32


@pytest.fixture
def fake_decode(monkeypatch):
    """Replace decode_opus_frames so tests don't need libopus.

    Concatenates frame payloads as-is; for the orchestrator's purposes
    the exact PCM contents don't matter beyond "non-empty when frames
    arrived, empty when none did".
    """

    def fake(frames, **kwargs):
        return b"".join(frames)

    monkeypatch.setattr(orchestrator, "decode_opus_frames", fake)
    return fake


@pytest.fixture
def fast_sleep(monkeypatch):
    real_sleep = asyncio.sleep

    async def fast(delay):
        await real_sleep(0)

    monkeypatch.setattr(orchestrator.asyncio, "sleep", fast)
    return fast


@pytest.fixture(autouse=True)
def _cleanup_recording_slot():
    """Always release the module-level recording slot between tests.

    The orchestrator opens/closes the slot itself, but a failed
    test that bypasses ``finally`` would leak state into the next
    test; this fixture defends against that.
    """
    yield
    if is_recording():
        stop_recording()


@pytest.mark.asyncio
async def test_pipeline_drives_listen_state_and_returns_text(fake_decode, monkeypatch):
    """Happy path: start/stop notifications fire, frames decode, engine runs."""
    # Compress the duration sleep so the test is fast without losing
    # the orchestrator's actual behaviour.
    real_sleep = asyncio.sleep

    async def fast_sleep(delay):
        await real_sleep(0)

    monkeypatch.setattr(orchestrator.asyncio, "sleep", fast_sleep)

    engine = _CapturingEngine(text="やっほー")
    frames = [b"opus_frame_0", b"opus_frame_1", b"opus_frame_2"]
    esp32 = _FakeESP32(frames_to_inject=frames)
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    result = await listen_and_transcribe(
        {"duration_ms": 500, "engine": "faster-whisper", "language": "ja"},
        gateway=gateway,
        registry=reg,
    )

    assert [s[0] for s in esp32.listen_states] == ["start", "stop"]
    # start was sent with mode="manual"; stop carries no mode.
    assert esp32.listen_states[0] == ("start", "manual")
    assert esp32.listen_states[1] == ("stop", None)

    assert result["engine"] == "faster-whisper"
    assert result["text"] == "やっほー"
    assert result["language"] == "ja"
    assert result["frame_count"] == 3
    assert result["duration_ms"] == 3 * DEVICE_FRAME_DURATION_MS
    assert result["sample_rate"] == DEVICE_SAMPLE_RATE

    # Engine saw the concatenated PCM (our fake decode just glued the
    # frame payloads together).
    assert len(engine.calls) == 1
    pcm_arg, opts = engine.calls[0]
    assert pcm_arg == b"".join(frames)
    assert opts["language"] == "ja"

    # Recording slot was closed cleanly.
    assert not is_recording()


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("motion", "expected_tool_calls"),
    [
        ("none", []),
        (
            "face-only",
            [
                ("self.display.set_avatar", {"face": "thinking"}),
                ("self.display.set_avatar", {"face": "idle"}),
            ],
        ),
        (
            "look-up",
            [
                ("self.robot.get_head_angles", {}),
                ("self.robot.set_head_angles", {"yaw": 12.0, "pitch": 50.0}),
                ("self.display.set_avatar", {"face": "thinking"}),
            ],
        ),
    ],
)
async def test_listen_motion_success_paths(
    fake_decode,
    fast_sleep,
    motion,
    expected_tool_calls,
):
    """Each motion mode preserves its success cleanup/hold contract."""
    engine = _CapturingEngine(text="ok")
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    result = await listen_and_transcribe(
        {"duration_ms": 500, "motion": motion},
        gateway=gateway,
        registry=reg,
    )

    assert result["text"] == "ok"
    assert [s[0] for s in esp32.listen_states] == ["start", "stop"]
    assert esp32.tool_calls == expected_tool_calls
    if motion == "look-up":
        assert esp32.head_pitch == 50.0
    else:
        assert esp32.head_pitch == 24.0
    assert not is_recording()


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("motion", "expected_tool_calls"),
    [
        ("none", []),
        (
            "face-only",
            [
                ("self.display.set_avatar", {"face": "thinking"}),
                ("self.display.set_avatar", {"face": "idle"}),
            ],
        ),
        (
            "look-up",
            [
                ("self.robot.get_head_angles", {}),
                ("self.robot.set_head_angles", {"yaw": 12.0, "pitch": 50.0}),
                ("self.display.set_avatar", {"face": "thinking"}),
                ("self.robot.set_head_angles", {"yaw": 12.0, "pitch": 24.0}),
                ("self.display.set_avatar", {"face": "idle"}),
            ],
        ),
    ],
)
async def test_listen_motion_failure_paths(
    fake_decode,
    fast_sleep,
    motion,
    expected_tool_calls,
):
    """Failures clean up avatar state and roll back look-up pitch."""
    engine = _RaisingEngine(TimeoutError("model timed out"))
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError, match="failed"):
        await listen_and_transcribe(
            {"duration_ms": 500, "motion": motion},
            gateway=gateway,
            registry=reg,
        )

    assert [s[0] for s in esp32.listen_states] == ["start", "stop"]
    assert esp32.tool_calls == expected_tool_calls
    assert esp32.head_pitch == 24.0
    assert not is_recording()


@pytest.mark.asyncio
@pytest.mark.parametrize("motion", ["none", "face-only", "look-up"])
async def test_listen_motion_validation_error_paths(motion):
    """look_up_pitch is validated before any device-side call."""
    engine = _CapturingEngine()
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(ValueError, match="look_up_pitch"):
        await listen_and_transcribe(
            {"duration_ms": 500, "motion": motion, "look_up_pitch": 4.0},
            gateway=gateway,
            registry=reg,
        )

    assert esp32.listen_states == []
    assert esp32.tool_calls == []
    assert engine.calls == []
    assert not is_recording()


@pytest.mark.asyncio
async def test_listen_motion_rejects_unknown_mode():
    """Unknown motion values fail before any device-side call."""
    engine = _CapturingEngine()
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(ValueError, match="motion"):
        await listen_and_transcribe(
            {"duration_ms": 500, "motion": "nod"},
            gateway=gateway,
            registry=reg,
        )

    assert esp32.listen_states == []
    assert esp32.tool_calls == []
    assert engine.calls == []
    assert not is_recording()


@pytest.mark.asyncio
@pytest.mark.parametrize("motion", ["face-only", "look-up"])
async def test_listen_motion_cleanup_completes_under_cancellation(
    fake_decode,
    motion,
):
    """Cancellation during capture must not bypass motion cleanup.

    The avatar / head rollback driven by ``_finish_listen_motion``
    must complete before the orchestrator's ``listen_lock`` is
    released; otherwise a follow-up listen() can race with an
    in-flight cleanup and observe a partially-restored device state.

    The lock-ordering invariant is verified directly: a competing
    waiter races for ``listen_lock`` while ``listen_and_transcribe``
    is cancelled, and records cleanup observability at the exact
    moment it acquires the lock. With proper cancellation handling
    in ``_shield_listen_motion_cleanup`` the orchestrator only exits
    its ``async with lock_ctx`` block after cleanup completes, so
    the waiter sees the cleanup events already set. With a naïve
    ``except Exception`` wrapper around ``asyncio.shield(...)`` the
    cleanup runs as an orphan task after the lock is released, and
    the waiter would acquire the lock with the events still unset.
    """
    engine = _CapturingEngine(text="ok")
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    cleanup_idle_observed = asyncio.Event()
    cleanup_pitch_restored = asyncio.Event()
    original_call_tool = esp32.call_tool

    async def slow_cleanup(name, arguments):
        result_pair = await original_call_tool(name, arguments)
        # Add a small delay specifically on the cleanup-path calls
        # so we can observe whether the orchestrator waits for them.
        if name == "self.display.set_avatar" and arguments.get("face") == "idle":
            await asyncio.sleep(0.02)
            cleanup_idle_observed.set()
        if name == "self.robot.set_head_angles" and arguments.get("pitch") == 24.0:
            await asyncio.sleep(0.02)
            cleanup_pitch_restored.set()
        return result_pair

    esp32.call_tool = slow_cleanup

    reg = EngineRegistry()
    reg.register(engine)

    listen_task = asyncio.create_task(
        listen_and_transcribe(
            {"duration_ms": 200, "motion": motion},
            gateway=gateway,
            registry=reg,
        )
    )

    # Let the orchestrator enter the capture window and acquire
    # listen_lock before launching the waiter / cancelling.
    await asyncio.sleep(0.02)

    waiter_snapshot: dict[str, bool] = {}

    async def waiter() -> None:
        async with esp32.listen_lock:
            # Snapshot at the exact moment the lock is acquired —
            # this is the moment the buggy implementation would let
            # the waiter through while cleanup is still in flight.
            waiter_snapshot["idle"] = cleanup_idle_observed.is_set()
            waiter_snapshot["pitch"] = cleanup_pitch_restored.is_set()

    waiter_task = asyncio.create_task(waiter())
    # Yield once so the waiter registers its lock request before the
    # orchestrator releases the lock.
    await asyncio.sleep(0)

    async def re_cancel() -> None:
        # Re-cancel while the orchestrator is already inside the
        # motion-cleanup await. With a naïve
        # ``try / await asyncio.shield(coro()) / except Exception``
        # wrapper this second cancellation raises CancelledError at
        # the cleanup await, propagates past the ``except Exception``,
        # and orphans the in-flight cleanup before the listen_lock is
        # released — which is the precise regression this test
        # protects against. The fixed wrapper holds the cleanup task
        # in scope and re-awaits it under shield, so cleanup still
        # completes before the function returns.
        await asyncio.sleep(0.005)
        if not listen_task.done():
            listen_task.cancel()

    re_cancel_task = asyncio.create_task(re_cancel())

    listen_task.cancel()

    with pytest.raises(asyncio.CancelledError):
        await listen_task
    await waiter_task
    await re_cancel_task

    assert waiter_snapshot["idle"], (
        "set_avatar('idle') must complete BEFORE listen_lock is released "
        "to the competing waiter — orphan cleanup race detected"
    )
    if motion == "look-up":
        assert waiter_snapshot["pitch"], (
            "saved pitch must be restored BEFORE listen_lock is released "
            "to the competing waiter"
        )
    assert not is_recording()


@pytest.mark.asyncio
async def test_listen_motion_look_up_re_cancel_during_cleanup_chains_rollback_failure(
    fake_decode,
):
    """Cancellation re-arriving during the motion-cleanup await
    combined with a rollback failure must still surface the
    rollback error.

    The first cancel propagates through the capture sleep and lands
    the orchestrator in the motion-cleanup await. A second
    ``listen_task.cancel()`` from a sibling task fires while
    ``_shield_listen_motion_cleanup`` is waiting on the slow
    rollback ``set_head_angles``, exercising the cancellation
    branch of the wrapper. Without explicit chaining the wrapper
    would raise a fresh ``CancelledError`` and discard the
    ``cleanup_error``, hiding the physical-state mismatch from the
    caller. The fix chains via ``raise CancelledError() from
    cleanup_error`` so the caller can inspect both.
    """
    engine = _CapturingEngine(text="ok")
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    original_call_tool = esp32.call_tool

    async def slow_failing_rollback(name, arguments):
        # Slow the rollback so the re-cancel lands while the
        # cleanup_task is still in flight.
        if (
            name == "self.robot.set_head_angles"
            and arguments.get("pitch") == 24.0
        ):
            await asyncio.sleep(0.03)
            esp32.tool_calls.append((name, dict(arguments)))
            return {}, {"message": "simulated rollback pitch failure"}
        return await original_call_tool(name, arguments)

    esp32.call_tool = slow_failing_rollback

    reg = EngineRegistry()
    reg.register(engine)

    listen_task = asyncio.create_task(
        listen_and_transcribe(
            {"duration_ms": 200, "motion": "look-up"},
            gateway=gateway,
            registry=reg,
        )
    )

    # Let the orchestrator enter the capture window.
    await asyncio.sleep(0.02)

    async def re_cancel() -> None:
        # Re-cancel after the first cancel has propagated past the
        # capture sleep and the orchestrator is inside
        # ``_shield_listen_motion_cleanup``'s shield-await loop.
        await asyncio.sleep(0.005)
        if not listen_task.done():
            listen_task.cancel()

    re_cancel_task = asyncio.create_task(re_cancel())
    listen_task.cancel()

    with pytest.raises(asyncio.CancelledError) as exc_info:
        await listen_task
    await re_cancel_task

    # Cleanup failure must be chained onto the cancellation —
    # otherwise the cancellation branch silently swallows the
    # rollback failure and the caller has no programmatic signal
    # about a potentially off-baseline pose.
    primary = exc_info.value
    chained = primary.__cause__
    assert chained is not None, (
        "rollback failure must be chained onto the cancellation even "
        "when _shield_listen_motion_cleanup raises a fresh CancelledError"
    )
    assert "set_head_angles" in str(chained), (
        f"chained error should reference the rollback head-angles failure; "
        f"got {chained!r}"
    )


@pytest.mark.asyncio
async def test_listen_motion_look_up_double_cleanup_failure_preserves_both_errors(
    fake_decode,
    fast_sleep,
):
    """Both pitch-rollback and avatar-restore failures must remain
    inspectable to the caller.

    In ``_finish_listen_motion`` the pitch rollback runs in a ``try``
    and the avatar restore in the matching ``finally`` so the avatar
    always runs. When both raise, the avatar failure overrides as
    the primary cleanup error (Python finally semantics) and the
    pitch failure is preserved on ``__context__`` via automatic
    exception-context tracking. The outer ``finally`` then chains
    the avatar failure onto the listen failure via ``__cause__``,
    so the caller can navigate the chain as

        primary  ⟶ __cause__ (avatar)  ⟶ __context__ (pitch)

    and see both physical-state concerns without losing either.
    """
    engine = _RaisingEngine(TimeoutError("engine fail"))
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    original_call_tool = esp32.call_tool

    async def double_cleanup_failure(name, arguments):
        # Rollback set_head_angles(saved pitch=24.0) fails.
        if (
            name == "self.robot.set_head_angles"
            and arguments.get("pitch") == 24.0
        ):
            esp32.tool_calls.append((name, dict(arguments)))
            return {}, {"message": "rollback pitch failure"}
        # Cleanup-path set_avatar('idle') also fails.
        if (
            name == "self.display.set_avatar"
            and arguments.get("face") == "idle"
        ):
            esp32.tool_calls.append((name, dict(arguments)))
            return {}, {"message": "rollback avatar failure"}
        return await original_call_tool(name, arguments)

    esp32.call_tool = double_cleanup_failure

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError) as exc_info:
        await listen_and_transcribe(
            {"duration_ms": 500, "motion": "look-up"},
            gateway=gateway,
            registry=reg,
        )

    primary = exc_info.value
    assert "engine" in str(primary).lower(), (
        f"primary should reference the engine failure; got {primary!r}"
    )

    # The avatar-restore failure became the cleanup primary (it
    # overrode the pitch failure via the try/finally raise sequence
    # in _finish_listen_motion) and is chained via __cause__.
    chained = primary.__cause__
    assert chained is not None, "cleanup chain must reach the caller"
    assert "set_avatar" in str(chained), (
        f"primary cleanup error should be the avatar restore failure; "
        f"got {chained!r}"
    )

    # The pitch failure is preserved on the avatar failure's
    # __context__ via Python's automatic exception-context tracking,
    # so the caller can navigate to it programmatically.
    pitch_failure = chained.__context__
    assert pitch_failure is not None, (
        "pitch rollback failure must remain inspectable via "
        "__cause__.__context__"
    )
    assert "set_head_angles" in str(pitch_failure), (
        f"pitch failure should appear on __context__; got {pitch_failure!r}"
    )

    # Both cleanup attempts were made on the device.
    pitches = [
        args.get("pitch")
        for name, args in esp32.tool_calls
        if name == "self.robot.set_head_angles"
    ]
    faces = [
        args.get("face")
        for name, args in esp32.tool_calls
        if name == "self.display.set_avatar"
    ]
    assert 24.0 in pitches, "pitch rollback was attempted"
    assert "idle" in faces, "idle avatar restore was attempted"


@pytest.mark.asyncio
async def test_listen_motion_look_up_engine_failure_with_rollback_failure_chains(
    fake_decode,
    fast_sleep,
):
    """When the STT engine fails mid-capture AND the rollback also
    fails for motion='look-up', the caller must see both errors.

    The engine failure is the primary cause (it triggered the
    rollback attempt). The rollback failure is chained via
    ``__cause__`` from the outer listen() finally so the caller
    can detect that physical state may be off-baseline even though
    the original primary cause references the engine timeout.
    Without this chaining the rollback failure would vanish into
    ``logger.warning`` and the caller would only see the engine
    error with no signal about the device pose.
    """
    engine = _RaisingEngine(TimeoutError("model timed out"))
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    original_call_tool = esp32.call_tool

    async def rollback_only_failure(name, arguments):
        # Forward set_head_angles uses pitch=50.0 (look_up_pitch),
        # forward set_avatar uses face='thinking'. Both stay on the
        # original fake path. Only the rollback set_head_angles
        # (saved pitch=24.0) fails here.
        if (
            name == "self.robot.set_head_angles"
            and arguments.get("pitch") == 24.0
        ):
            esp32.tool_calls.append((name, dict(arguments)))
            return {}, {"message": "simulated rollback pitch failure"}
        return await original_call_tool(name, arguments)

    esp32.call_tool = rollback_only_failure

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError) as exc_info:
        await listen_and_transcribe(
            {"duration_ms": 500, "motion": "look-up"},
            gateway=gateway,
            registry=reg,
        )

    # The engine failure is wrapped into a RuntimeError stating
    # which engine failed; it remains the primary exception so the
    # caller's first signal is still "what triggered the failure".
    primary = exc_info.value
    assert "STT engine" in str(primary) or "engine" in str(primary).lower(), (
        f"primary error should reference the engine failure; got {primary!r}"
    )

    # The rollback failure is chained via ``__cause__`` so the
    # caller can inspect it programmatically without it disappearing
    # into a logger.warning. (The engine's original TimeoutError is
    # still reachable through ``__context__`` thanks to Python's
    # automatic exception-context tracking.)
    chained = primary.__cause__
    assert chained is not None, (
        "rollback failure must be chained onto the listen failure, "
        "not silently swallowed"
    )
    assert "set_head_angles" in str(chained), (
        f"chained error should reference the rollback head-angles failure; "
        f"got {chained!r}"
    )

    # Sanity: forward motion ran (pitch=50, thinking) and rollback
    # was attempted (pitch=24 — that's the call we made fail).
    pitches_attempted = [
        args.get("pitch")
        for name, args in esp32.tool_calls
        if name == "self.robot.set_head_angles"
    ]
    assert 50.0 in pitches_attempted
    assert 24.0 in pitches_attempted


@pytest.mark.asyncio
async def test_listen_motion_look_up_nested_partial_failure_surfaces_rollback_error(
    fake_decode,
    fast_sleep,
):
    """Nested partial failure during look-up setup must surface BOTH
    errors to the caller.

    Setup sequence: ``set_head_angles(look_up_pitch)`` succeeds, then
    ``set_avatar('thinking')`` fails. The orchestrator's setup-failure
    branch attempts a rollback ``set_head_angles(saved)`` which here
    is configured to fail as well. Without chaining, the caller only
    sees the forward avatar error while the device remains in the
    look-up pose with no signal that physical state was altered.
    The fix chains the cleanup error onto the forward exception via
    ``raise ... from`` so the caller can observe both.
    """
    engine = _CapturingEngine(text="ok")
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    original_call_tool = esp32.call_tool

    async def double_failure(name, arguments):
        # Forward set_avatar('thinking') fails — record attempt.
        if (
            name == "self.display.set_avatar"
            and arguments.get("face") == "thinking"
        ):
            esp32.tool_calls.append((name, dict(arguments)))
            return {}, {"message": "simulated forward avatar failure"}
        # Rollback set_head_angles(saved pitch=24.0) fails — record attempt.
        if (
            name == "self.robot.set_head_angles"
            and arguments.get("pitch") == 24.0
        ):
            esp32.tool_calls.append((name, dict(arguments)))
            return {}, {"message": "simulated rollback pitch failure"}
        # Forward set_head_angles(50.0), forward get_head_angles, and
        # any other tool call uses the normal fake-call path which
        # records into tool_calls on its own.
        return await original_call_tool(name, arguments)

    esp32.call_tool = double_failure

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError) as exc_info:
        await listen_and_transcribe(
            {"duration_ms": 500, "motion": "look-up"},
            gateway=gateway,
            registry=reg,
        )

    # The primary exception is the forward avatar failure.
    primary = exc_info.value
    assert "set_avatar" in str(primary) or "avatar" in str(primary), (
        f"primary error should reference the forward avatar failure; got {primary!r}"
    )

    # The rollback failure is chained via ``__cause__`` so the caller
    # can inspect both. Without chaining the rollback failure would
    # vanish into a logger.warning and the device would be left in
    # the look-up pose with no programmatic signal.
    chained = primary.__cause__
    assert chained is not None, (
        "rollback failure must be chained onto the forward failure, "
        "not silently swallowed"
    )
    assert "set_head_angles" in str(chained), (
        f"chained error should reference the rollback head-angles failure; got {chained!r}"
    )

    # Both the forward attempt and the rollback attempt were actually
    # made (the fake recorded both).
    avatars_attempted = [
        args.get("face")
        for name, args in esp32.tool_calls
        if name == "self.display.set_avatar"
    ]
    assert "thinking" in avatars_attempted, "forward thinking avatar was attempted"
    pitches_attempted = [
        args.get("pitch")
        for name, args in esp32.tool_calls
        if name == "self.robot.set_head_angles"
    ]
    assert 50.0 in pitches_attempted, "forward look_up pitch was attempted"
    assert 24.0 in pitches_attempted, "rollback saved pitch was attempted"


@pytest.mark.asyncio
async def test_listen_motion_look_up_partial_rollback_still_restores_avatar(
    fake_decode,
    fast_sleep,
):
    """If the pitch rollback fails during look-up cleanup, the avatar
    restore must still run.

    Otherwise a failed rollback (e.g. servo bus dropped, device
    error) would leave the device visibly stuck on the ``thinking``
    face even though the listen itself already failed.
    """
    engine = _RaisingEngine(TimeoutError("model timed out"))
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    original_call_tool = esp32.call_tool
    avatar_idle_observed = asyncio.Event()

    async def selective_rollback_failure(name, arguments):
        # Fail only on the rollback set_head_angles call (saved
        # pitch=24.0); the forward set_head_angles for look-up uses
        # pitch=50.0 and stays on the original fake path. Record the
        # failing call into tool_calls explicitly so the assertion
        # below can verify the rollback was actually attempted.
        if (
            name == "self.robot.set_head_angles"
            and arguments.get("pitch") == 24.0
        ):
            esp32.tool_calls.append((name, dict(arguments)))
            return {}, {"message": "simulated rollback failure"}
        result_pair = await original_call_tool(name, arguments)
        if name == "self.display.set_avatar" and arguments.get("face") == "idle":
            avatar_idle_observed.set()
        return result_pair

    esp32.call_tool = selective_rollback_failure

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError):
        await listen_and_transcribe(
            {"duration_ms": 500, "motion": "look-up"},
            gateway=gateway,
            registry=reg,
        )

    # The rollback ``set_head_angles`` was actually attempted (and
    # rejected by the fake).
    assert any(
        name == "self.robot.set_head_angles" and args.get("pitch") == 24.0
        for name, args in esp32.tool_calls
    ), "pitch rollback should be attempted before the avatar restore"

    # The avatar restore must run regardless of the pitch failure —
    # this is the user-visible UX contract: failed listen leaves the
    # device on the idle face, never stuck on the listening face.
    assert avatar_idle_observed.is_set(), (
        "set_avatar('idle') must run even when the pitch rollback raises"
    )


@pytest.mark.asyncio
async def test_pipeline_returns_empty_text_on_no_frames(fake_decode, monkeypatch):
    """An empty capture (no frames) returns text='' rather than erroring.

    Useful when a user goes silent for the full window: faster-whisper
    on an empty buffer would otherwise spend cycles producing noise,
    and treating "no frames" as a failure would surface as a confusing
    MCP error.
    """
    real_sleep = asyncio.sleep

    async def fast_sleep(delay):
        await real_sleep(0)

    monkeypatch.setattr(orchestrator.asyncio, "sleep", fast_sleep)

    engine = _CapturingEngine()
    esp32 = _FakeESP32(frames_to_inject=[])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    result = await listen_and_transcribe(
        {"duration_ms": 200},
        gateway=gateway,
        registry=reg,
    )

    assert result["frame_count"] == 0
    assert result["duration_ms"] == 0
    assert result["text"] == ""
    # Engine is NOT invoked when the buffer is empty — wasted work.
    assert engine.calls == []


@pytest.mark.asyncio
async def test_pipeline_blocks_protocol_v2(fake_decode):
    """Devices that negotiated WebSocket protocol v2 are blocked."""
    engine = _CapturingEngine()
    esp32 = _FakeESP32(protocol_version=2)
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError, match=r"v1"):
        await listen_and_transcribe(
            {"duration_ms": 500},
            gateway=gateway,
            registry=reg,
        )

    # No notifications, no engine call, slot stays clean.
    assert esp32.listen_states == []
    assert engine.calls == []
    assert not is_recording()


@pytest.mark.asyncio
async def test_pipeline_raises_when_device_disconnected():
    """Disconnected device fails fast without invoking the engine."""
    engine = _CapturingEngine()
    esp32 = _FakeESP32(connected=False)
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError, match="ESP32"):
        await listen_and_transcribe(
            {"duration_ms": 500},
            gateway=gateway,
            registry=reg,
        )

    assert engine.calls == []
    assert not is_recording()


@pytest.mark.asyncio
async def test_pipeline_declines_when_device_driven_capture_active():
    """MCP listen() declines when the audio_stream slot is already held.

    Symmetric to the device-driven listen.start branch in
    esp32_client._handler, which logs and bails when an MCP listen() is
    already recording. Without this guard the orchestrator's
    ``start_recording(session_id)`` silently overwrites the active
    buffer, dropping the device-driven capture frames mid-stream.
    """
    # Simulate a device-driven capture already holding the slot.
    start_recording("device-session-xyz")
    assert is_recording()

    engine = _CapturingEngine()
    esp32 = _FakeESP32()
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError, match=r"declined"):
        await listen_and_transcribe(
            {"duration_ms": 500},
            gateway=gateway,
            registry=reg,
        )

    # The pre-existing slot is preserved: no listen.start was sent, no
    # engine call ran, and the device-driven buffer was not clobbered
    # (still owned by the device session). The autouse cleanup fixture
    # releases it after the test.
    assert esp32.listen_states == []
    assert engine.calls == []
    assert is_recording()


@pytest.mark.asyncio
async def test_pipeline_translates_disconnect_before_listen_start(fake_decode, monkeypatch):
    """ConnectionError on listen.start surfaces as a clear RuntimeError."""

    real_sleep = asyncio.sleep

    async def fast_sleep(delay):
        await real_sleep(0)

    monkeypatch.setattr(orchestrator.asyncio, "sleep", fast_sleep)

    class FailingESP32(_FakeESP32):
        async def send_listen_state(self, state: str, mode: str = "manual") -> None:
            self.listen_states.append((state, mode if state == "start" else None))
            if state == "start":
                raise ConnectionError("device dropped during listen.start")

    engine = _CapturingEngine()
    esp32 = FailingESP32()
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError, match="listen.start"):
        await listen_and_transcribe(
            {"duration_ms": 500},
            gateway=gateway,
            registry=reg,
        )

    # Recording slot must be closed even when start fails.
    assert not is_recording()
    assert engine.calls == []


@pytest.mark.asyncio
async def test_pipeline_translates_engine_error_to_runtime_error(fake_decode, monkeypatch):
    """Engine failure surfaces as RuntimeError with the cause preserved."""

    real_sleep = asyncio.sleep

    async def fast_sleep(delay):
        await real_sleep(0)

    monkeypatch.setattr(orchestrator.asyncio, "sleep", fast_sleep)

    cause = TimeoutError("model load timed out")
    engine = _RaisingEngine(cause)
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(RuntimeError) as exc_info:
        await listen_and_transcribe(
            {"duration_ms": 500},
            gateway=gateway,
            registry=reg,
        )

    assert "faster-whisper" in str(exc_info.value).lower()
    assert exc_info.value.__cause__ is cause
    # listen.stop was attempted even though transcribe failed (frames
    # arrived, slot needs to drain on the device side).
    assert ("stop", None) in esp32.listen_states
    assert not is_recording()


@pytest.mark.asyncio
async def test_pipeline_value_error_propagates_as_value_error(fake_decode, monkeypatch):
    """ValueError from the engine stays a ValueError."""

    real_sleep = asyncio.sleep

    async def fast_sleep(delay):
        await real_sleep(0)

    monkeypatch.setattr(orchestrator.asyncio, "sleep", fast_sleep)

    engine = _RaisingEngine(ValueError("bad language hint"))
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    with pytest.raises(ValueError, match="language"):
        await listen_and_transcribe(
            {"duration_ms": 500},
            gateway=gateway,
            registry=reg,
        )


@pytest.mark.asyncio
async def test_pipeline_sends_listen_stop_on_cancellation(fake_decode):
    """A cancelled listen() call still tells the device to stop.

    Without ``asyncio.shield`` around the listen.stop send, the
    cancellation would propagate before the stop reached the wire and
    the firmware would stay in ``kDeviceStateListening`` with the
    microphone open until an unrelated button press / wake-word
    eventually pulled it back to idle. The shielded stop guarantees
    the device receives the cleanup notification even when the
    orchestrator coroutine itself is being torn down.
    """
    engine = _CapturingEngine()
    esp32 = _FakeESP32()  # no frame injection; the sleep will be cancelled
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    task = asyncio.create_task(
        listen_and_transcribe(
            {"duration_ms": 30000},  # long window; we will cancel mid-flight
            gateway=gateway,
            registry=reg,
        )
    )
    # Yield once so the task starts, lands in listen.start, then
    # enters the capture sleep.
    await asyncio.sleep(0)
    await asyncio.sleep(0)
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task

    # Despite the cancellation, the orchestrator must have delivered
    # both listen.start and listen.stop to the device so the firmware
    # leaves listening mode cleanly.
    state_seq = [s for s, _ in esp32.listen_states]
    assert "start" in state_seq
    assert "stop" in state_seq
    # The recording slot must also be released — leaving it open
    # would corrupt the next listen() call's buffer.
    assert not is_recording()
    # Engine is not invoked because the cancellation prevents the
    # post-capture transcribe step.
    assert engine.calls == []


@pytest.mark.asyncio
async def test_pipeline_serialises_concurrent_listen_calls(fake_decode, monkeypatch):
    """Concurrent listen() calls don't share the recording slot.

    Without the listen_lock, both calls would race ``start_recording`` /
    ``stop_recording`` against the single module-level slot, producing
    a mixed transcription. The lock guarantees a strictly sequential
    pattern: start_0 < stop_0 < start_1 < stop_1.
    """
    real_sleep = asyncio.sleep

    async def fast_sleep(delay):
        await real_sleep(0)

    monkeypatch.setattr(orchestrator.asyncio, "sleep", fast_sleep)

    engine = _CapturingEngine()
    esp32 = _FakeESP32(frames_to_inject=[b"opus_a"])
    gateway = _FakeGateway(esp32)

    reg = EngineRegistry()
    reg.register(engine)

    await asyncio.gather(
        listen_and_transcribe(
            {"duration_ms": 200}, gateway=gateway, registry=reg
        ),
        listen_and_transcribe(
            {"duration_ms": 200}, gateway=gateway, registry=reg
        ),
    )

    state_seq = [s for s, _ in esp32.listen_states]
    start_indices = [i for i, s in enumerate(state_seq) if s == "start"]
    stop_indices = [i for i, s in enumerate(state_seq) if s == "stop"]
    assert len(start_indices) == 2
    assert len(stop_indices) == 2
    # The lock guarantees: start_0 < stop_0 < start_1 < stop_1.
    assert (
        start_indices[0]
        < stop_indices[0]
        < start_indices[1]
        < stop_indices[1]
    )
    assert not is_recording()


def test_resolve_listen_language_defaults_to_ja(monkeypatch):
    monkeypatch.delenv("STACKCHAN_LISTEN_LANGUAGE", raising=False)
    assert orchestrator.resolve_listen_language({}) == "ja"


def test_resolve_listen_language_uses_env_when_omitted(monkeypatch):
    monkeypatch.setenv("STACKCHAN_LISTEN_LANGUAGE", "en")
    assert orchestrator.resolve_listen_language({}) == "en"


def test_resolve_listen_language_argument_wins(monkeypatch):
    monkeypatch.setenv("STACKCHAN_LISTEN_LANGUAGE", "en")
    assert orchestrator.resolve_listen_language({"language": "ja"}) == "ja"
