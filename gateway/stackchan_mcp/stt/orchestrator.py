"""STT orchestration: drive listening, collect frames, decode, transcribe.

The orchestrator is the glue between the ``listen`` MCP tool (defined
in :mod:`stackchan_mcp.stdio_server`) and the STT engine implementations
registered in :mod:`stackchan_mcp.stt`. For each call it:

1. Validates arguments.
2. Looks up the requested engine.
3. Acquires the device's listen lock so two concurrent ``listen()``
   invocations cannot overlap (they would otherwise both buffer
   inbound Opus frames into the same capture and produce a mixed
   transcription).
4. Switches the audio_stream module into recording mode so binary
   frames stop being discarded and start being buffered.
5. Sends ``{"type":"listen","state":"start","mode":"manual"}`` to put
   the device firmware into listening state and stream microphone
   Opus frames up the existing WebSocket.
6. Waits ``duration_ms`` (the capture window).
7. Sends ``{"type":"listen","state":"stop"}`` to drop the device back
   to idle and stop the inbound frame stream.
8. Decodes the buffered Opus frames into 16 kHz mono PCM and hands
   the blob off to the engine for transcription.
9. Returns ``{ text, duration_ms, language, frame_count }`` to the
   MCP client.

Symmetric to :mod:`stackchan_mcp.tts.orchestrator` — same error-class
discipline (``ValueError`` for bad arguments, ``NotImplementedError``
for missing engine, ``RuntimeError`` for runtime failures) and the
same protocol-v1 gate.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
from contextlib import nullcontext
from typing import TYPE_CHECKING, Any, Literal

from ..audio_stream import (
    capture_lock,
    is_recording,
    recording_owner,
    start_recording,
    stop_recording,
)
from .audio_utils import DEVICE_FRAME_DURATION_MS, DEVICE_SAMPLE_RATE, decode_opus_frames
from .base import EngineRegistry, get_registry

if TYPE_CHECKING:
    from ..gateway import Gateway

logger = logging.getLogger(__name__)


#: Default engine name when ``engine`` is omitted from the tool call.
#: faster-whisper runs locally and matches the "works offline out of
#: the box" stance (Issue #91).
DEFAULT_ENGINE = "faster-whisper"

#: Default ISO 639-1 language when ``listen()`` omits ``language``.
#: Override with ``STACKCHAN_LISTEN_LANGUAGE`` (e.g. ``en``).
LISTEN_LANGUAGE_ENV = "STACKCHAN_LISTEN_LANGUAGE"
DEFAULT_LISTEN_LANGUAGE = "ja"


def resolve_listen_language(arguments: dict[str, Any]) -> Any:
    """Return the listen language: explicit argument, then env, then ``ja``."""
    if "language" in arguments:
        return arguments["language"]
    env = os.getenv(LISTEN_LANGUAGE_ENV, "").strip()
    return env or DEFAULT_LISTEN_LANGUAGE

#: Minimum capture window. Below this Whisper has too little signal to
#: produce anything useful, and the listen() round-trip starts to be
#: dominated by setup overhead.
MIN_DURATION_MS = 100

#: Maximum capture window. 30 seconds is enough for any single-shot
#: utterance and caps Python memory at ~960 KB of PCM (16000 * 2 *
#: 30) which is safe even on a Raspberry Pi gateway.
MAX_DURATION_MS = 30000

#: Small grace period after sending ``listen.start`` before we start
#: counting the capture window, mirrored from the TTS orchestrator's
#: ``TTS_START_TRANSITION_DELAY_S``. The firmware dispatches the state
#: transition through ``Schedule()`` (queued onto the main task) so
#: the first inbound frame can race the ``kDeviceStateListening``
#: transition; 50 ms is well above typical scheduling latency.
LISTEN_START_TRANSITION_DELAY_S = 0.05

LISTENING_FACE = "thinking"
IDLE_FACE = "idle"
LISTEN_MOTIONS = {"none", "face-only", "look-up"}
BEAT_MODE_OWNER = "beat_mode"
BEAT_MODE_OWNER_PREFIX = f"{BEAT_MODE_OWNER}:"


def _is_beat_mode_owner(owner: str | None) -> bool:
    return owner == BEAT_MODE_OWNER or (
        owner is not None and owner.startswith(BEAT_MODE_OWNER_PREFIX)
    )


MIN_LOOK_UP_PITCH = 5.0
MAX_LOOK_UP_PITCH = 85.0


def _validate_motion_args(
    arguments: dict[str, Any],
) -> tuple[Literal["none", "face-only", "look-up"], float]:
    motion = arguments.get("motion", "none")
    if not isinstance(motion, str) or motion not in LISTEN_MOTIONS:
        raise ValueError(
            "'motion' must be one of 'none', 'face-only', or 'look-up'; "
            f"got {motion!r}"
        )

    look_up_pitch_raw = arguments.get("look_up_pitch", 50.0)
    if isinstance(look_up_pitch_raw, bool) or not isinstance(
        look_up_pitch_raw, int | float
    ):
        raise ValueError(
            "'look_up_pitch' must be a number between 5 and 85; got "
            + repr(look_up_pitch_raw)
        )

    look_up_pitch = float(look_up_pitch_raw)
    if look_up_pitch < MIN_LOOK_UP_PITCH or look_up_pitch > MAX_LOOK_UP_PITCH:
        raise ValueError(
            "'look_up_pitch' must be between 5 and 85; got "
            f"{look_up_pitch_raw!r}"
        )

    return motion, look_up_pitch


async def _call_tool_for_device(
    esp32: Any, name: str, arguments: dict[str, Any], device_id: str | None
) -> tuple[Any, dict[str, Any] | None]:
    """``esp32.call_tool``, routed to ``device_id`` when given.

    Omitting ``device_id`` (None) makes the exact same 2-arg call as
    before phase2, so existing test doubles that don't accept a
    ``device_id`` keyword keep working unchanged.
    """
    if device_id is None:
        return await esp32.call_tool(name, arguments)
    return await esp32.call_tool(name, arguments, device_id=device_id)


async def _send_listen_state_for_device(
    esp32: Any, state: str, device_id: str | None, **kwargs: Any
) -> None:
    """``esp32.send_listen_state``, routed to ``device_id`` when given."""
    if device_id is None:
        await esp32.send_listen_state(state, **kwargs)
    else:
        await esp32.send_listen_state(state, device_id=device_id, **kwargs)


def _listen_lock_for_device(esp32: Any, device_id: str | None) -> Any:
    """Resolve the STT lock for ``device_id`` with defensive fallbacks."""
    getter = getattr(esp32, "listen_lock_for", None)
    if getter is not None:
        return getter(device_id)
    return getattr(esp32, "listen_lock", None)


async def _shield_listen_motion_cleanup(
    gateway: "Gateway",
    motion: Literal["none", "face-only", "look-up"],
    saved_angles: tuple[float, float] | None,
    *,
    succeeded: bool,
    device_id: str | None = None,
) -> BaseException | None:
    """Wait for motion cleanup to complete even under cancellation.

    A bare ``await asyncio.shield(coro())`` protects the inner
    coroutine from cancellation, but a cancellation propagating
    through the awaiter is re-raised immediately — which would
    release ``listen_lock`` while the device-side cleanup is still
    in flight, and leave the cleanup as an orphan task whose
    failure nobody observes. Hold the cleanup task in scope,
    re-await it under shield through repeated cancellations, then
    surface the cancellation once cleanup has finished so the
    caller still sees ``CancelledError``.

    Non-cancellation cleanup failures are both logged at warning
    level and returned to the caller, so partial-failure paths
    (e.g. setup-time motion failure followed by a rollback failure)
    can chain the cleanup error onto the primary error rather than
    silently swallow a physical-state mismatch.
    """
    cleanup_task = asyncio.create_task(
        _finish_listen_motion(
            gateway,
            motion,
            saved_angles,
            succeeded=succeeded,
            device_id=device_id,
        )
    )

    outer_cancelled = False
    cleanup_error: BaseException | None = None
    while True:
        try:
            await asyncio.shield(cleanup_task)
        except asyncio.CancelledError:
            outer_cancelled = True
            continue
        except Exception as exc:
            cleanup_error = exc
            logger.warning(
                "best-effort listen motion cleanup failed: %s", exc
            )
        break

    if outer_cancelled:
        if cleanup_error is not None:
            # The outer task was cancelled WHILE rollback itself
            # also failed. Chain the cleanup failure via
            # ``__cause__`` so the caller still gets a programmatic
            # signal that the device may be off-baseline, instead
            # of seeing only a fresh ``CancelledError`` and silently
            # losing the rollback error.
            raise asyncio.CancelledError() from cleanup_error
        raise asyncio.CancelledError()
    return cleanup_error


async def _call_device_tool(
    gateway: "Gateway",
    name: str,
    arguments: dict[str, Any],
    device_id: str | None = None,
) -> Any:
    result, error = await _call_tool_for_device(
        gateway.esp32, name, arguments, device_id
    )
    if error:
        message = error.get("message", error) if isinstance(error, dict) else error
        raise RuntimeError(f"Device tool '{name}' failed: {message}")
    return result


def _extract_head_angles(result: Any) -> tuple[float, float]:
    payload = result
    if isinstance(result, dict) and "content" in result:
        content = result.get("content") or []
        if content:
            text = content[0].get("text") if isinstance(content[0], dict) else None
            if isinstance(text, str):
                payload = json.loads(text)

    if not isinstance(payload, dict):
        raise RuntimeError("Device tool 'self.robot.get_head_angles' returned no angles")

    yaw = payload.get("yaw")
    pitch = payload.get("pitch")
    if not isinstance(yaw, int | float) or not isinstance(pitch, int | float):
        raise RuntimeError("Device tool 'self.robot.get_head_angles' returned invalid angles")
    return float(yaw), float(pitch)


async def _set_avatar(
    gateway: "Gateway", face: str, device_id: str | None = None
) -> None:
    await _call_device_tool(
        gateway, "self.display.set_avatar", {"face": face}, device_id
    )


async def _set_head_angles(
    gateway: "Gateway", *, yaw: float, pitch: float, device_id: str | None = None
) -> None:
    await _call_device_tool(
        gateway,
        "self.robot.set_head_angles",
        {"yaw": yaw, "pitch": pitch},
        device_id,
    )


async def _begin_listen_motion(
    gateway: "Gateway",
    motion: Literal["none", "face-only", "look-up"],
    look_up_pitch: float,
    device_id: str | None = None,
) -> tuple[float, float] | None:
    if motion == "none":
        return None
    if motion == "face-only":
        await _set_avatar(gateway, LISTENING_FACE, device_id)
        return None

    result = await _call_device_tool(
        gateway, "self.robot.get_head_angles", {}, device_id
    )
    yaw, pitch = _extract_head_angles(result)
    try:
        await _set_head_angles(gateway, yaw=yaw, pitch=look_up_pitch, device_id=device_id)
        await _set_avatar(gateway, LISTENING_FACE, device_id)
    except Exception as forward_exc:
        cleanup_error = await _shield_listen_motion_cleanup(
            gateway,
            motion,
            (yaw, pitch),
            succeeded=False,
            device_id=device_id,
        )
        if cleanup_error is not None:
            # Forward setup failed AND rollback also failed — the
            # device may still be in the look-up pose. Chain the
            # cleanup error onto the forward exception so the caller
            # sees both physical-state concerns instead of just the
            # forward avatar / motion error.
            raise forward_exc from cleanup_error
        raise
    return yaw, pitch


async def _finish_listen_motion(
    gateway: "Gateway",
    motion: Literal["none", "face-only", "look-up"],
    saved_angles: tuple[float, float] | None,
    *,
    succeeded: bool,
    device_id: str | None = None,
) -> None:
    if motion == "none":
        return
    if motion == "face-only":
        await _set_avatar(gateway, IDLE_FACE, device_id)
        return
    if succeeded or saved_angles is None:
        return

    yaw, pitch = saved_angles
    try:
        await _set_head_angles(gateway, yaw=yaw, pitch=pitch, device_id=device_id)
    finally:
        # Restore the avatar regardless of whether the pitch rollback
        # succeeded — otherwise a failed ``set_head_angles`` would
        # leave the device visibly stuck on the ``thinking`` face
        # even though the listen itself already failed.
        await _set_avatar(gateway, IDLE_FACE, device_id)


async def listen_and_transcribe(
    arguments: dict[str, Any],
    *,
    gateway: "Gateway | None" = None,
    registry: EngineRegistry | None = None,
    device_id: str | None = None,
) -> dict[str, Any]:
    """Capture a short utterance from the device and transcribe it.

    Args:
        arguments: MCP tool arguments. Recognised keys:

            * ``duration_ms`` (optional, default 5000): capture window
              in milliseconds, clamped to
              [:data:`MIN_DURATION_MS`, :data:`MAX_DURATION_MS`].
            * ``engine``: engine name; defaults to
              :data:`DEFAULT_ENGINE`.
            * ``language``: ISO 639-1 code (e.g. ``"ja"``) or ``None``
              for autodetect.
            * ``model``: engine-specific model identifier (e.g.
              ``"base"`` / ``"small"`` for faster-whisper).

        gateway: The :class:`Gateway` instance whose ESP32 manager
            this call drives. Required for the pipeline; left optional
            in the signature so callers can inspect validation errors
            without setting up a gateway (e.g. argument-validation
            tests).

        registry: Engine registry to look up ``engine`` in. Defaults
            to the process-wide registry. Tests inject a fresh
            registry to avoid leaking state across cases.

    Returns:
        Dict describing the transcription: ``engine``, ``text``,
        ``language``, ``duration_ms``, ``frame_count``.

    Raises:
        ValueError: bad arguments.
        NotImplementedError: requested engine not registered.
        RuntimeError: no gateway / no device / wrong protocol /
            device disconnected mid-capture.
    """
    duration_raw = arguments.get("duration_ms", 5000)
    if isinstance(duration_raw, bool) or not isinstance(duration_raw, int):
        raise ValueError(
            "'duration_ms' must be an integer; got " + repr(duration_raw)
        )
    if duration_raw < MIN_DURATION_MS or duration_raw > MAX_DURATION_MS:
        raise ValueError(
            f"'duration_ms' must be between {MIN_DURATION_MS} and "
            f"{MAX_DURATION_MS}; got {duration_raw}"
        )
    motion, look_up_pitch = _validate_motion_args(arguments)

    engine_raw = arguments.get("engine", DEFAULT_ENGINE)
    engine_name = (
        engine_raw if isinstance(engine_raw, str) and engine_raw else DEFAULT_ENGINE
    )

    reg = registry if registry is not None else get_registry()
    engine = reg.get(engine_name)
    if engine is None:
        available = reg.names()
        raise NotImplementedError(
            f"STT engine '{engine_name}' is not registered. "
            f"Available engines: {available or '(none)'}. "
            "Install the relevant extra (e.g. "
            "'pip install stackchan-mcp[stt-faster-whisper]' for the "
            "default local engine, or 'pip install "
            "stackchan-mcp[stt-openai]' for the OpenAI Whisper API)."
        )

    if gateway is None:
        raise RuntimeError(
            "listen_and_transcribe requires a 'gateway' argument to "
            "drive the device's listening state; this call appears to "
            "be a validation probe without one."
        )

    if device_id is None:
        if not gateway.esp32.device_connected:
            raise RuntimeError(
                "No ESP32 device connected; cannot capture audio for STT."
            )
    else:
        connection_for = getattr(gateway.esp32, "connection_for", None)
        target_conn = connection_for(device_id) if connection_for is not None else None
        if target_conn is None:
            raise RuntimeError(
                f"No ESP32 device connected: device_id={device_id!r}"
            )

    # Protocol version gate, identical in spirit to the TTS side
    # (PR #75). The gateway's inbound binary handler decodes raw Opus
    # only on protocol v1; v2/v3 wrap the binary message in a
    # BinaryProtocol header that this gateway does not yet parse on
    # the inbound side either, so the buffered frames would be
    # unusable.
    connection_for = getattr(gateway.esp32, "connection_for", None)
    connection = (
        connection_for(device_id)
        if connection_for is not None
        else getattr(gateway.esp32, "connection", None)
    )
    proto_version = getattr(connection, "protocol_version", 1)
    if proto_version != 1:
        raise RuntimeError(
            f"listen() requires WebSocket protocol v1, but the connected "
            f"device negotiated v{proto_version}. Rebuild the firmware "
            "with v1 (the default for this repository) — v2/v3 "
            "BinaryProtocol header wrapping is not yet supported on the "
            "STT path."
        )

    # Acquire the device's listen lock (then the gateway-wide capture lock,
    # since the recording slot is shared by all devices) so two concurrent
    # listen() calls cannot interleave their capture windows. Same getattr fallback
    # pattern as the TTS orchestrator's ``tts_lock`` so test fakes that
    # don't expose the attribute keep working.
    listen_lock = _listen_lock_for_device(gateway.esp32, device_id)
    lock_ctx = listen_lock if listen_lock is not None else nullcontext()

    if is_recording() and _is_beat_mode_owner(recording_owner()):
        raise RuntimeError(
            "beat mode is already using the device microphone; stop "
            "beat mode before calling listen()"
        )

    duration_ms = int(duration_raw)
    language = resolve_listen_language(arguments)
    model = arguments.get("model")

    frame_count = 0
    pcm = b""
    actual_duration_ms = 0
    motion_saved_angles: tuple[float, float] | None = None
    succeeded = False

    async with lock_ctx, capture_lock():
        connection_for = getattr(gateway.esp32, "connection_for", None)
        connection = (
            connection_for(device_id)
            if connection_for is not None
            else gateway.esp32.connection
        )
        session_id = getattr(connection, "session_id", "") if connection else ""

        # Symmetric ownership guard with the device-driven listen.start
        # branch in esp32_client._handler (which logs and bails when an
        # MCP listen() already holds the slot). If a device-driven
        # capture is currently buffering, decline this MCP listen() call
        # rather than silently clobber the in-progress buffer via
        # start_recording's slot-overwrite. lock_ctx serialises MCP-side
        # listen acquisitions, but the device-driven path acquires the
        # slot from esp32_client without going through lock_ctx, so this
        # check is the cross-source guard.
        if is_recording():
            owner = recording_owner()
            if _is_beat_mode_owner(owner):
                raise RuntimeError(
                    "beat mode is already using the device microphone; stop "
                    "beat mode before calling listen()"
                )
            raise RuntimeError(
                "audio_stream recording slot is already held "
                "(device-driven capture in progress); MCP listen() "
                "declined to avoid clobbering the active buffer"
            )

        primary_exc: BaseException | None = None
        try:
            motion_saved_angles = await _begin_listen_motion(
                gateway, motion, look_up_pitch, device_id
            )

            # Switch the audio_stream module into recording mode BEFORE
            # sending listen.start so we don't drop the first frame the
            # device emits the moment it lands in kDeviceStateListening.
            start_recording(session_id)
            listen_start_sent = False
            try:
                try:
                    await _send_listen_state_for_device(
                        gateway.esp32, "start", device_id, mode="manual"
                    )
                    listen_start_sent = True
                except ConnectionError as exc:
                    raise RuntimeError(
                        f"Device disconnected before listen.start: {exc}"
                    ) from exc

                # Wait for the firmware's state machine to land in
                # kDeviceStateListening before we start counting the
                # capture window (same rationale as the TTS pipeline's
                # ``TTS_START_TRANSITION_DELAY_S``).
                await asyncio.sleep(LISTEN_START_TRANSITION_DELAY_S)

                await asyncio.sleep(duration_ms / 1000.0)
            finally:
                # Cancellation-safe listen.stop. If the request is
                # cancelled mid-capture (or any exception unwinds here)
                # after listen.start has been delivered, the device is
                # still in ``kDeviceStateListening`` with the microphone
                # open — without a best-effort stop the firmware would
                # stay there until an unrelated user action (button /
                # wake-word) eventually pulled it back to idle.
                # ``asyncio.shield`` protects the stop send from the
                # cancellation that's already propagating through the
                # outer await, so the device receives the stop even
                # though the orchestrator coroutine itself is being torn
                # down. The shielded send still completes synchronously
                # before this ``finally`` block returns.
                if listen_start_sent:
                    try:
                        await asyncio.shield(
                            _send_listen_state_for_device(
                                gateway.esp32, "stop", device_id
                            )
                        )
                    except (ConnectionError, asyncio.CancelledError):
                        # Device dropped, or our awaiter was cancelled
                        # after shield released the send back to us. In
                        # both cases the partial buffer is still worth
                        # transcribing, but the operator should know the
                        # firmware may need a manual nudge.
                        logger.warning(
                            "listen.stop did not reach device cleanly "
                            "(cancellation or disconnect); firmware may "
                            "stay in listening mode until a button press "
                            "or wake-word"
                        )
                    except Exception as exc:
                        logger.warning(
                            "best-effort listen.stop failed: %s", exc
                        )
                frames = stop_recording()

            frame_count = len(frames)
            if frame_count == 0:
                # Distinguish "device disconnected with no frames" (likely
                # protocol mismatch / firmware not yet supporting listen)
                # from "spoken nothing". The latter is a legitimate empty
                # transcription and not surfaced as an error.
                logger.info(
                    "listen(): no Opus frames received during %d ms window",
                    duration_ms,
                )

            try:
                pcm = decode_opus_frames(frames)
            except Exception as exc:
                raise RuntimeError(f"Opus decode failed: {exc}") from exc

            actual_duration_ms = frame_count * DEVICE_FRAME_DURATION_MS

            if pcm:
                try:
                    result = await engine.transcribe(
                        pcm,
                        language=language,
                        model=model,
                    )
                except ValueError:
                    raise
                except Exception as exc:
                    raise RuntimeError(
                        f"STT engine '{engine_name}' failed: {exc}"
                    ) from exc
            else:
                # Empty capture — return an empty transcription rather
                # than failing the call. ``language`` falls back to the
                # caller's hint (or empty string).
                result = {
                    "text": "",
                    "language": (
                        language if isinstance(language, str) and language else ""
                    ),
                }
            succeeded = True
        except BaseException as exc:
            # Capture the listen-body failure so the cleanup helper
            # can chain a rollback failure onto it below if cleanup
            # also raises. Without this, a rollback failure on top
            # of an already-failed listen would vanish into a
            # ``logger.warning`` and the caller would only see the
            # listen failure with no signal that physical state may
            # be off-baseline.
            primary_exc = exc
            raise
        finally:
            cleanup_error = await _shield_listen_motion_cleanup(
                gateway,
                motion,
                motion_saved_angles,
                succeeded=succeeded,
                device_id=device_id,
            )
            if cleanup_error is not None:
                if primary_exc is not None:
                    # Listen body already failed AND rollback also
                    # failed — chain via ``__cause__`` so the
                    # caller sees both physical-state concerns. The
                    # listen failure stays primary (it triggered the
                    # rollback attempt); the rollback failure is
                    # exposed via ``__cause__`` for inspection. Any
                    # pre-existing ``__cause__`` on the listen
                    # failure (e.g. engine's TimeoutError chained
                    # to a RuntimeError) is preserved on
                    # ``__context__`` automatically.
                    raise primary_exc from cleanup_error
                # Cleanup itself failed on an otherwise-successful
                # listen — surface it so the failure doesn't vanish
                # into a logger.warning while the device may be
                # off-baseline.
                raise cleanup_error

    logger.info(
        "listen(): engine=%s frames=%d duration_ms=%d text=%r",
        engine_name,
        frame_count,
        actual_duration_ms,
        (result.get("text", "") or "")[:80],
    )

    return {
        "engine": engine_name,
        "text": result.get("text", ""),
        "language": result.get("language", ""),
        "duration_ms": actual_duration_ms,
        "frame_count": frame_count,
        "sample_rate": DEVICE_SAMPLE_RATE,
    }
