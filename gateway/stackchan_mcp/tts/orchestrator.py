"""TTS orchestration: pick an engine, synthesise, encode, and push.

The orchestrator is the glue between the ``say`` MCP tool (defined in
:mod:`stackchan_mcp.stdio_server`) and the engine implementations
registered in :mod:`stackchan_mcp.tts`. It validates arguments, looks
up an engine, runs the synthesis, encodes the result to Opus, and
hands the frames off to :mod:`stackchan_mcp.audio_stream` for delivery.

The framework half (Engine ABC, registry, validation surface) shipped
in PR1 of Issue #70; PR2 wires the actual VOICEVOX → PCM → Opus →
WebSocket pipeline. The signature stays back-compatible with PR1's
tests: ``gateway`` is keyword-only and may be omitted, in which case
calls that pass validation surface a clear error instead of silently
synthesising audio with no destination.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
from collections.abc import AsyncIterator, Awaitable, Callable
from contextlib import nullcontext
from typing import TYPE_CHECKING, Any

from .audio_utils import (
    DEVICE_CHANNELS,
    DEVICE_FRAME_DURATION_MS,
    DEVICE_SAMPLE_RATE,
    encode_opus_frames,
    resample_pcm16_linear,
)
from .base import EngineRegistry, get_registry
from .emoji_expression import detect_emoji_face, strip_emoji_for_plain_tts

if TYPE_CHECKING:
    from ..gateway import Gateway

#: Delay between the ``tts.start`` notification and the first audio
#: frame, in seconds. Firmware dispatches the state transition through
#: ``Schedule()`` (queued onto the main task), so the first frame can
#: race the ``kDeviceStateSpeaking`` transition and be discarded by
#: ``OnIncomingAudio``. 50 ms is well above typical scheduling latency
#: but well below human-perceptible delay.
TTS_START_TRANSITION_DELAY_S = 0.05

#: Delay after the ``tts.stop`` notification before reasserting an
#: emoji-selected face. Firmware handles stop by scheduling the idle /
#: lip-sync reset on its main task, so this short settle delay lets that
#: queued restore land before the gateway sends the final face update.
TTS_STOP_FACE_REDISPATCH_SETTLE_DELAY_S = 0.05

logger = logging.getLogger(__name__)


#: Built-in default engine name when ``voice`` is omitted from the tool
#: call and ``STACKCHAN_TTS_ENGINE`` is unset. VOICEVOX is the canonical
#: default (Issue #70).
DEFAULT_VOICE = "voicevox"

#: Environment variable that overrides the default engine selected when a
#: ``say`` call omits ``voice``. The per-call ``voice`` argument still
#: takes precedence over this; this only changes the fallback when no
#: ``voice`` is given. Unset → :data:`DEFAULT_VOICE`.
TTS_ENGINE_ENV_VAR = "STACKCHAN_TTS_ENGINE"


def _extract_set_avatar_payload(result: Any) -> dict[str, Any] | None:
    payload = result
    if isinstance(result, dict) and "content" in result:
        content = result.get("content") or []
        if isinstance(content, list) and content:
            text = (
                content[0].get("text")
                if isinstance(content[0], dict)
                else None
            )
            if isinstance(text, str):
                try:
                    payload = json.loads(text)
                except json.JSONDecodeError:
                    return None

    return payload if isinstance(payload, dict) else None


def _set_avatar_payload_error(payload: dict[str, Any]) -> str:
    raw_error = payload.get("error")
    if isinstance(raw_error, dict):
        message = raw_error.get("message")
        if isinstance(message, str) and message.strip():
            return message
    elif isinstance(raw_error, str) and raw_error.strip():
        return raw_error
    elif raw_error:
        return str(raw_error)

    return "set_avatar reported ok=false"


def _resolve_default_engine() -> str:
    """Return the default engine name, honouring ``STACKCHAN_TTS_ENGINE``.

    The environment variable lets an operator make a non-VOICEVOX engine
    (e.g. ``irodori``) the default for ``say`` calls that don't pass an
    explicit ``voice``. A blank or whitespace-only value is ignored so an
    empty export does not silently break engine lookup.
    """
    env_engine = os.getenv(TTS_ENGINE_ENV_VAR)
    if env_engine and env_engine.strip():
        return env_engine.strip()
    return DEFAULT_VOICE


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


async def _send_tts_state_for_device(
    esp32: Any, state: str, device_id: str | None
) -> None:
    """``esp32.send_tts_state``, routed to ``device_id`` when given."""
    if device_id is None:
        await esp32.send_tts_state(state)
    else:
        await esp32.send_tts_state(state, device_id=device_id)


async def _send_audio_frame_for_device(
    esp32: Any, frame: bytes, device_id: str | None
) -> None:
    """``esp32.send_audio_frame``, routed to ``device_id`` when given."""
    if device_id is None:
        await esp32.send_audio_frame(frame)
    else:
        await esp32.send_audio_frame(frame, device_id=device_id)


def _tts_lock_for_device(esp32: Any, device_id: str | None) -> Any:
    """Resolve the TTS lock for ``device_id`` with defensive fallbacks.

    Prefers the new :meth:`ESP32Manager.tts_lock_for` accessor; falls
    back to the pre-phase2 ``tts_lock`` property for test doubles that
    predate per-device locks (only reachable when ``device_id`` is None,
    since callers only pass an explicit id once a real per-device
    connection resolved it).
    """
    getter = getattr(esp32, "tts_lock_for", None)
    if getter is not None:
        return getter(device_id)
    return getattr(esp32, "tts_lock", None)


async def _try_set_avatar_face(
    gateway: "Gateway",
    face: str,
    device_id: str | None = None,
) -> tuple[bool, str | None]:
    try:
        result, error = await _call_tool_for_device(
            gateway.esp32, "self.display.set_avatar", {"face": face}, device_id
        )
    except Exception as exc:
        logger.warning("say(): set_avatar(%s) failed: %s", face, exc)
        return False, str(exc)

    if error:
        message = error.get("message", error) if isinstance(error, dict) else error
        logger.warning("say(): set_avatar(%s) failed: %s", face, message)
        return False, str(message)

    payload = _extract_set_avatar_payload(result)
    if payload is not None and payload.get("ok") is False:
        message = _set_avatar_payload_error(payload)
        logger.warning(
            "say(): set_avatar(%s) reported ok=false: %s", face, message
        )
        return False, message

    return True, None


async def _try_set_avatar_face_with_tts_lock(
    gateway: "Gateway",
    face: str,
    device_id: str | None = None,
) -> tuple[bool, str | None]:
    tts_lock = _tts_lock_for_device(gateway.esp32, device_id)
    lock_ctx = tts_lock if tts_lock is not None else nullcontext()

    async with lock_ctx:
        return await _try_set_avatar_face(gateway, face, device_id)


async def synthesize_and_send(
    arguments: dict[str, Any],
    *,
    gateway: "Gateway | None" = None,
    registry: EngineRegistry | None = None,
    device_id: str | None = None,
) -> dict[str, Any]:
    """Synthesise text via a registered engine and push it to the device.

    Args:
        arguments: MCP tool arguments. Recognised keys:

            * ``text`` (required): non-empty string to speak.
            * ``voice``: engine name; when omitted, the default is
              resolved from ``STACKCHAN_TTS_ENGINE`` and otherwise
              :data:`DEFAULT_VOICE`.
            * ``speaker_id``: engine-specific speaker identifier
              (e.g. VOICEVOX speaker).
            * ``reference_audio``: path to a reference audio sample
              (e.g. for Irodori voice cloning, PR3).

        gateway: The :class:`Gateway` instance whose
            :attr:`Gateway.esp32` the audio frames are pushed through.
            Required for the pipeline; left optional in the signature
            so callers can inspect validation errors without setting
            up a gateway (e.g. argument-validation tests).

        registry: Engine registry to look up ``voice`` in. Defaults to
            the process-wide registry. Tests inject a fresh registry
            here to avoid leaking state across cases.

    Returns:
        Dict describing the synthesis: ``engine``, ``text``,
        ``speaker_id``, ``frame_count``, ``sample_rate``,
        ``frame_duration_ms``, ``duration_ms``, plus emoji-expression
        metadata such as ``face`` and ``text_stripped``.

    Raises:
        ValueError: if ``text`` is missing / empty / non-string.
        NotImplementedError: if no engine is registered under ``voice``.
            The message lists the registered engines so callers can
            tell whether they need to install an extra (e.g.
            ``pip install stackchan-mcp[tts]``) or pick a different
            ``voice``.
        RuntimeError: if ``gateway`` is omitted, or if no ESP32 device
            is connected when the orchestrator tries to push frames.
    """
    # Validation runs first so callers can probe argument shape without
    # a real gateway / engine.
    text = arguments.get("text", "")
    if not isinstance(text, str) or not text.strip():
        raise ValueError("'text' is required and must be a non-empty string")

    face = detect_emoji_face(text)

    # An explicit, non-empty ``voice`` argument always wins. Otherwise the
    # default engine is resolved from STACKCHAN_TTS_ENGINE (falling back to
    # DEFAULT_VOICE), so an operator can switch the default without every
    # caller passing ``voice``.
    voice_raw = arguments.get("voice")
    voice = (
        voice_raw
        if isinstance(voice_raw, str) and voice_raw
        else _resolve_default_engine()
    )

    reg = registry if registry is not None else get_registry()
    engine = reg.get(voice)

    if engine is None:
        available = reg.names()
        raise NotImplementedError(
            f"TTS engine '{voice}' is not registered. "
            f"Available engines: {available or '(none)'}. "
            "Install the relevant extra (e.g. "
            "'pip install stackchan-mcp[tts]' for VOICEVOX) and ensure "
            "the corresponding service (e.g. the VOICEVOX HTTP engine) "
            "is reachable."
        )

    if gateway is None:
        raise RuntimeError(
            "synthesize_and_send requires a 'gateway' argument to push "
            "audio frames; this call appears to be a validation probe "
            "without one."
        )

    if device_id is None:
        if not gateway.esp32.device_connected:
            raise RuntimeError(
                "No ESP32 device connected; cannot deliver synthesised audio."
            )
    else:
        connection_for = getattr(gateway.esp32, "connection_for", None)
        target_conn = connection_for(device_id) if connection_for is not None else None
        if target_conn is None:
            raise RuntimeError(
                f"No ESP32 device connected: device_id={device_id!r}"
            )

    speaker_id = arguments.get("speaker_id")
    speaker_name = arguments.get("speaker_name")
    reference_audio = arguments.get("reference_audio")

    plain_tts_text = strip_emoji_for_plain_tts(text)
    tts_text = text
    text_stripped = False
    if not getattr(engine, "supports_emoji_style", False):
        text_stripped = plain_tts_text != text
        tts_text = plain_tts_text

    face_dispatched = False
    face_error: str | None = None
    face_redispatched = False
    face_redispatch_error: str | None = None
    should_redispatch_face_after_speech = (
        face is not None and bool(tts_text.strip())
    )

    if not tts_text.strip():
        if face is not None:
            face_dispatched, face_error = await _try_set_avatar_face_with_tts_lock(
                gateway,
                face,
                device_id,
            )
        logger.info(
            "say(): engine=%s speaker=%s speech skipped: text empty after "
            "emoji strip",
            voice,
            speaker_id if speaker_id is not None else "default",
        )
        result = {
            "engine": voice,
            "text": text,
            "speaker_id": speaker_id,
            "frame_count": 0,
            "sample_rate": DEVICE_SAMPLE_RATE,
            "frame_duration_ms": DEVICE_FRAME_DURATION_MS,
            "duration_ms": 0,
            "face": face,
            "face_dispatched": face_dispatched,
            "face_error": face_error,
            "face_redispatched": face_redispatched,
            "face_redispatch_error": face_redispatch_error,
            "text_stripped": text_stripped,
            "spoke": False,
            "reason": "text empty after emoji strip",
        }
        if text_stripped:
            result["tts_text"] = tts_text
        return result

    # WebSocket protocol version gate. The firmware decodes raw Opus
    # binary frames only on protocol v1; v2/v3 wrap each binary message
    # in a BinaryProtocol header that this gateway does not yet emit.
    # Streaming raw frames to a v2/v3 device makes the firmware parse
    # Opus bytes as header fields, so the audio never plays — yet
    # without this check ``say()`` would still report success. Fail
    # fast with a clear, actionable error instead. BinaryProtocol
    # header wrapping is tracked as a follow-up to Issue #70.
    connection_for = getattr(gateway.esp32, "connection_for", None)
    connection = (
        connection_for(device_id)
        if connection_for is not None
        else getattr(gateway.esp32, "connection", None)
    )
    proto_version = getattr(connection, "protocol_version", 1)
    if proto_version != 1:
        raise RuntimeError(
            f"TTS requires WebSocket protocol v1, but the connected "
            f"device negotiated v{proto_version}. Rebuild the firmware "
            "with v1 (the default for this repository) — v2/v3 "
            "BinaryProtocol header wrapping is not yet supported."
        )

    # Engine failures (HTTP errors from VOICEVOX, malformed WAV from
    # the synthesiser, etc.) are translated to RuntimeError so the
    # MCP layer's narrow exception filter still produces clean error
    # JSON. Validation errors (ValueError) are kept distinct so bad
    # arguments stay separable from operational degradation.
    try:
        pcm = await engine.synthesize(
            tts_text,
            speaker_id=speaker_id,
            speaker_name=speaker_name,
            reference_audio=reference_audio,
        )
    except ValueError:
        raise
    except Exception as exc:
        raise RuntimeError(
            f"TTS engine '{voice}' failed: {exc}"
        ) from exc

    if not pcm:
        # An engine returning no PCM is a bug, not a runtime condition;
        # surface it to the caller rather than silently sending zero
        # frames (which would look like the device "ignored" the call).
        raise RuntimeError(
            f"Engine '{voice}' produced no PCM data for the given text."
        )

    async def dispatch_face_before_first_frame() -> None:
        nonlocal face_dispatched, face_error
        if face is not None:
            face_dispatched, face_error = await _try_set_avatar_face(
                gateway,
                face,
                device_id,
            )

    async def redispatch_face_after_playback() -> None:
        nonlocal face_redispatched, face_redispatch_error
        if face is None:
            return
        await asyncio.sleep(TTS_STOP_FACE_REDISPATCH_SETTLE_DELAY_S)
        face_redispatched, face_redispatch_error = await _try_set_avatar_face(
            gateway,
            face,
            device_id,
        )

    # Hand the PCM off to the shared encode-and-push path. Engines that
    # have already resampled to DEVICE_SAMPLE_RATE (the documented
    # TTSEngine contract) need no further conversion here.
    result = await send_pcm_audio(
        gateway,
        pcm,
        source_label=f"engine:{voice}",
        before_first_frame=(
            dispatch_face_before_first_frame if face is not None else None
        ),
        after_playback_complete=(
            redispatch_face_after_playback
            if should_redispatch_face_after_speech
            else None
        ),
        device_id=device_id,
    )

    logger.info(
        "say(): engine=%s speaker=%s frames=%d duration_ms=%d",
        voice,
        speaker_id if speaker_id is not None else "default",
        result["frame_count"],
        result["duration_ms"],
    )

    response = {
        "engine": voice,
        "text": text,
        "speaker_id": speaker_id,
        "frame_count": result["frame_count"],
        "sample_rate": result["sample_rate"],
        "frame_duration_ms": result["frame_duration_ms"],
        "duration_ms": result["duration_ms"],
        "face": face,
        "face_dispatched": face_dispatched,
        "face_error": face_error,
        "face_redispatched": face_redispatched,
        "face_redispatch_error": face_redispatch_error,
        "text_stripped": text_stripped,
        "spoke": True,
    }
    if text_stripped:
        response["tts_text"] = tts_text
    return response


async def send_pcm_audio(
    gateway: "Gateway",
    pcm: bytes,
    *,
    source_rate: int = DEVICE_SAMPLE_RATE,
    source_label: str = "external",
    before_first_frame: Callable[[], Awaitable[None]] | None = None,
    after_playback_complete: Callable[[], Awaitable[None]] | None = None,
    device_id: str | None = None,
) -> dict[str, Any]:
    """Encode mono PCM and push as Opus frames to the connected device.

    This is the shared back-half of the TTS pipeline. ``synthesize_and_send``
    delegates here after running its engine; external producers (an HTTP
    PCM bridge, a sound-effect player, another voice stack like the SAIVerse
    voice-tts addon) can call this directly to push pre-synthesised audio
    without going through a registered :class:`TTSEngine`.

    Args:
        gateway: The :class:`Gateway` instance whose
            :attr:`Gateway.esp32` the audio frames are pushed through.
        pcm: Signed-16-bit little-endian mono PCM bytes. Must be
            non-empty.
        source_rate: Sample rate of ``pcm``. Defaults to
            :data:`DEVICE_SAMPLE_RATE` (16 kHz). When the source is at a
            different rate (e.g. voice-tts produces 32 kHz) the bytes
            are resampled linearly before Opus encoding; engines that
            already resample to the device rate internally should leave
            this at the default.
        source_label: Label that appears in the orchestrator log line so
            external callers can be traced separately from engine-driven
            synthesis (e.g. ``"voice-tts"``, ``"sfx:notification"``).
        before_first_frame: Internal hook for ``say()`` side effects that
            must be serialized with speech delivery.
        after_playback_complete: Internal hook for side effects that must
            run after the stop notification while the TTS lock is still
            held. Skipped if frame delivery is cancelled or interrupted.

    Returns:
        Dict describing the push: ``source``, ``frame_count``,
        ``sample_rate``, ``frame_duration_ms``, ``duration_ms``.
        ``sample_rate`` is always :data:`DEVICE_SAMPLE_RATE` because that
        is what the device actually decoded, regardless of the source
        rate.

    Raises:
        RuntimeError: if ``pcm`` is empty, ``gateway`` is missing, no
            device is connected, the negotiated protocol is not v1, Opus
            encoding fails, or the device disconnects mid-stream.
    """
    if not pcm:
        # Surface empty input as a clear bug rather than silently doing
        # nothing — same reasoning as the "engine produced no PCM" guard
        # in synthesize_and_send.
        raise RuntimeError(
            f"send_pcm_audio: PCM payload was empty (source={source_label!r})."
        )

    # Validate source_rate before it reaches resample_pcm16_linear.
    # The resampler computes ``n_dst = n_src * dst_rate // src_rate``,
    # which raises ZeroDivisionError on 0 and produces nonsense for
    # negatives — neither of which the caller's narrow ``RuntimeError``
    # filter translates cleanly to an MCP-facing error. Catch invalid
    # rates here so non-engine producers (HTTP /pcm bridges,
    # external voice stacks) that forward unvalidated request params
    # get a deterministic error instead of a raw stack trace.
    if not isinstance(source_rate, int) or source_rate <= 0:
        raise RuntimeError(
            f"send_pcm_audio: source_rate must be a positive integer, "
            f"got {source_rate!r}."
        )

    if gateway is None:
        raise RuntimeError(
            "send_pcm_audio requires a 'gateway' argument to push audio "
            "frames; this call appears to be a validation probe without one."
        )

    if device_id is None:
        if not gateway.esp32.device_connected:
            raise RuntimeError(
                "No ESP32 device connected; cannot deliver audio."
            )
    else:
        connection_for = getattr(gateway.esp32, "connection_for", None)
        target_conn = connection_for(device_id) if connection_for is not None else None
        if target_conn is None:
            raise RuntimeError(
                f"No ESP32 device connected: device_id={device_id!r}"
            )

    # WebSocket protocol version gate. The firmware decodes raw Opus
    # binary frames only on protocol v1; v2/v3 wrap each binary message
    # in a BinaryProtocol header that this gateway does not yet emit.
    connection_for = getattr(gateway.esp32, "connection_for", None)
    connection = (
        connection_for(device_id)
        if connection_for is not None
        else getattr(gateway.esp32, "connection", None)
    )
    proto_version = getattr(connection, "protocol_version", 1)
    if proto_version != 1:
        raise RuntimeError(
            f"send_pcm_audio requires WebSocket protocol v1, but the "
            f"connected device negotiated v{proto_version}. Rebuild the "
            "firmware with v1 (the default for this repository) — v2/v3 "
            "BinaryProtocol header wrapping is not yet supported."
        )

    # Resample to the device's rate before Opus encoding. ``encode_opus_frames``
    # expects samples at DEVICE_SAMPLE_RATE; passing a different rate would
    # produce frames that play back too fast / too slow on the device.
    if source_rate != DEVICE_SAMPLE_RATE:
        pcm = resample_pcm16_linear(pcm, source_rate, DEVICE_SAMPLE_RATE)

    # Encode -> push. Materialising the frame list before pushing keeps
    # the count reportable and makes it easy to short-circuit if Opus
    # encoding fails before any audio reaches the wire.
    try:
        opus_frames = list(encode_opus_frames(pcm))
    except Exception as exc:
        raise RuntimeError(f"Opus encoding failed: {exc}") from exc

    # Bracket the binary audio frames in TTS start/stop notifications.
    # The device firmware (Application::OnIncomingAudio) only accepts
    # binary audio frames while in kDeviceStateSpeaking, which is
    # entered on receipt of {"type":"tts","state":"start"} and exited
    # on "stop". Without these notifications the audio frames are
    # silently discarded.
    #
    # The whole start → frames → stop block runs under the device's
    # TTS lock so two concurrent pushes can't interleave their Opus
    # frames on the same WebSocket or overlap their state notifications.
    tts_lock = _tts_lock_for_device(gateway.esp32, device_id)
    lock_ctx = tts_lock if tts_lock is not None else nullcontext()

    sent = 0
    push_error: ConnectionError | None = None
    async with lock_ctx:
        try:
            await _send_tts_state_for_device(gateway.esp32, "start", device_id)
        except ConnectionError as exc:
            raise RuntimeError(
                f"Device disconnected before TTS start notification: {exc}"
            ) from exc

        # Wait for the firmware's state machine to land in
        # kDeviceStateSpeaking before sending the first frame.
        await asyncio.sleep(TTS_START_TRANSITION_DELAY_S)

        # Frame pacing: the device's decode queue holds at most ~40
        # frames (firmware MAX_DECODE_PACKETS_IN_QUEUE = 2400 /
        # OPUS_FRAME_DURATION_MS), and pushes that exceed it are
        # dropped silently. Send each frame at roughly the device's
        # consumption rate (one frame per frame_duration_ms) so a long
        # utterance never overflows. We let the loop drift by a single
        # interval if the network is slow — the wall clock is the
        # reference, not the loop iteration count.
        frame_period_s = DEVICE_FRAME_DURATION_MS / 1000.0
        loop = asyncio.get_event_loop()

        playback_complete = False
        try:
            if before_first_frame is not None:
                await before_first_frame()

            next_send_time = loop.time()
            for frame in opus_frames:
                now = loop.time()
                if now < next_send_time:
                    await asyncio.sleep(next_send_time - now)
                try:
                    await _send_audio_frame_for_device(gateway.esp32, frame, device_id)
                except ConnectionError as exc:
                    # Stop pushing on the first disconnect, but fall
                    # through to the stop notification (see finally) so
                    # that *if* the device is somehow still listening
                    # it returns to idle rather than staying in speaking
                    # forever.
                    push_error = exc
                    break
                sent += 1
                next_send_time += frame_period_s
            playback_complete = push_error is None
        finally:
            try:
                await _send_tts_state_for_device(gateway.esp32, "stop", device_id)
            except ConnectionError:
                # If the device dropped, it'll return to idle on its
                # own when the WebSocket close lands; nothing to do
                # here.
                pass

        if (
            playback_complete
            and push_error is None
            and after_playback_complete is not None
        ):
            await after_playback_complete()

    if push_error is not None:
        raise RuntimeError(
            f"Device disconnected after sending "
            f"{sent}/{len(opus_frames)} frames: {push_error}"
        ) from push_error

    duration_ms = sent * DEVICE_FRAME_DURATION_MS

    logger.info(
        "send_pcm_audio: source=%s frames=%d duration_ms=%d",
        source_label,
        sent,
        duration_ms,
    )

    return {
        "source": source_label,
        "frame_count": sent,
        "sample_rate": DEVICE_SAMPLE_RATE,
        "frame_duration_ms": DEVICE_FRAME_DURATION_MS,
        "duration_ms": duration_ms,
    }


async def send_pcm_stream(
    gateway: "Gateway",
    pcm_chunks: AsyncIterator[bytes],
    *,
    source_rate: int = DEVICE_SAMPLE_RATE,
    source_label: str = "stream",
) -> dict[str, Any]:
    """Encode and push PCM as it arrives from an async iterator.

    Where :func:`send_pcm_audio` buffers all PCM before encoding,
    ``send_pcm_stream`` accepts an :class:`~collections.abc.AsyncIterator`
    of PCM byte chunks and starts pushing Opus frames to the device as
    soon as enough samples have accumulated for one Opus frame. This
    keeps long utterances (multi-minute TTS, live audio mixes) playing
    on the device with low latency, without holding the entire PCM in
    memory.

    The Opus encoder instance is reused across chunks so the codec's
    internal state (predictors, gain) stays continuous — a fresh encoder
    per chunk would produce audible discontinuities at chunk
    boundaries.

    Args:
        gateway: The :class:`Gateway` instance whose
            :attr:`Gateway.esp32` the audio frames are pushed through.
        pcm_chunks: Async iterator yielding signed-16-bit LE mono PCM
            byte chunks. Chunk sizes need not be aligned to any boundary;
            the function buffers partial frames internally. Empty chunks
            are skipped without error so producers can use them as a
            "still alive" heartbeat. Iteration finishing (with no
            chunks left) flushes any trailing partial frame as
            zero-padded audio and ends the stream cleanly.
        source_rate: Sample rate of incoming PCM. Each chunk is
            resampled to :data:`DEVICE_SAMPLE_RATE` independently via
            linear interpolation; boundary discontinuities are
            negligible for speech-rate inputs.
        source_label: Label used in the orchestrator log so streaming
            producers can be traced separately (e.g.
            ``"voice-tts:msg_abc123"``).

    Returns:
        Dict describing the push: ``source``, ``frame_count``,
        ``sample_rate``, ``frame_duration_ms``, ``duration_ms``. Zero
        frames is a valid (logged-warning) outcome — e.g. the producer
        was cancelled before yielding any audio.

    Raises:
        RuntimeError: if ``gateway`` is missing, no device is connected,
            the negotiated protocol is not v1, opuslib is unavailable,
            Opus encoding fails, or the device disconnects mid-stream.
    """
    if gateway is None:
        raise RuntimeError(
            "send_pcm_stream requires a 'gateway' argument to push audio "
            "frames; this call appears to be a validation probe without one."
        )

    if not gateway.esp32.device_connected:
        raise RuntimeError(
            "No ESP32 device connected; cannot deliver streamed audio."
        )

    # WebSocket protocol version gate (same reasoning as send_pcm_audio).
    connection = getattr(gateway.esp32, "connection", None)
    proto_version = getattr(connection, "protocol_version", 1)
    if proto_version != 1:
        raise RuntimeError(
            f"send_pcm_stream requires WebSocket protocol v1, but the "
            f"connected device negotiated v{proto_version}. Rebuild the "
            "firmware with v1 (the default for this repository) — v2/v3 "
            "BinaryProtocol header wrapping is not yet supported."
        )

    # opuslib is the same optional extra used by ``encode_opus_frames``;
    # we hold the encoder instance across chunks here so importing
    # eagerly inside this function (rather than going via
    # ``encode_opus_frames``) gives the clearest install hint when the
    # extra is missing.
    try:
        import opuslib  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "opuslib is not installed. Install with "
            "'pip install stackchan-mcp[tts]' to enable streamed audio."
        ) from exc

    samples_per_frame = (
        DEVICE_SAMPLE_RATE * DEVICE_FRAME_DURATION_MS // 1000
    )
    bytes_per_frame = samples_per_frame * 2  # 16-bit
    # Number of source-rate samples that produce exactly one device-rate
    # opus frame after resampling. When the input is already at the device
    # rate this equals ``samples_per_frame`` and the resample step is a
    # no-op; otherwise we drain whole source-rate frames into the
    # resampler, which avoids the rounding-error and odd-byte issues that
    # per-chunk resampling has when transport chunk sizes are arbitrary.
    src_samples_per_frame = (
        source_rate * DEVICE_FRAME_DURATION_MS // 1000
    )
    if src_samples_per_frame <= 0:
        raise RuntimeError(
            f"source_rate {source_rate} is too low for "
            f"{DEVICE_FRAME_DURATION_MS} ms frames"
        )
    bytes_per_src_frame = src_samples_per_frame * 2  # 16-bit
    encoder = opuslib.Encoder(
        DEVICE_SAMPLE_RATE, DEVICE_CHANNELS, opuslib.APPLICATION_VOIP
    )

    tts_lock = getattr(gateway.esp32, "tts_lock", None)
    lock_ctx = tts_lock if tts_lock is not None else nullcontext()

    sent = 0
    push_error: ConnectionError | None = None
    # ``buffer`` accumulates source-rate PCM bytes. Chunks may be odd-byte
    # (HTTP chunked uploads can split a 16-bit sample across two transport
    # chunks), so we accumulate raw bytes here and only resample / encode
    # when the buffer holds at least one full source-rate frame's worth.
    buffer = bytearray()

    async def _push(opus_frame: bytes) -> bool:
        """Pace, send, advance counters. Returns False on disconnect."""
        nonlocal sent, push_error, next_send_time
        now = loop.time()
        if now < next_send_time:
            await asyncio.sleep(next_send_time - now)
        try:
            await gateway.esp32.send_audio_frame(opus_frame)
        except ConnectionError as exc:
            push_error = exc
            return False
        sent += 1
        # Advance the schedule from whichever is later: the previous
        # target (when we kept up — preserves the underlying 20 ms
        # cadence and absorbs sub-frame jitter), or the actual current
        # time (when the upstream producer paused — re-anchors pacing
        # so the next yielded chunk does not burst frames back-to-back
        # to "catch up" the stale schedule). Without this re-anchor a
        # producer pause longer than ``frame_period_s`` lets multiple
        # post-pause frames fire in one event-loop turn, exceeding the
        # firmware's ~40-packet decode queue and silently dropping
        # audio. The streaming use cases this helper is designed for
        # (HTTP chunked uploads, real-time TTS synthesis jitter)
        # routinely produce such pauses.
        next_send_time = max(next_send_time, loop.time()) + frame_period_s
        return True

    async with lock_ctx:
        try:
            await gateway.esp32.send_tts_state("start")
        except ConnectionError as exc:
            raise RuntimeError(
                f"Device disconnected before TTS start notification: {exc}"
            ) from exc

        await asyncio.sleep(TTS_START_TRANSITION_DELAY_S)

        frame_period_s = DEVICE_FRAME_DURATION_MS / 1000.0
        loop = asyncio.get_event_loop()
        next_send_time = loop.time()

        try:
            async for chunk in pcm_chunks:
                if not chunk:
                    # Empty chunk = heartbeat / cancellation tick; keep
                    # the loop alive without advancing the audio.
                    continue

                # Accumulate raw source-rate bytes. Resampling per chunk
                # used to live here but produced two bugs noted in PR
                # review: (a) ``resample_pcm16_linear`` raises ValueError
                # on odd-byte chunks because transport chunk boundaries
                # can split a 16-bit sample, and (b) rounding inside
                # ``resample_pcm16_linear`` (``n_dst = max(1, n_src *
                # dst_rate // src_rate)``) accumulates duration error
                # when called on small chunks repeatedly. Accumulating
                # to whole source-rate frames before resampling fixes
                # both.
                buffer.extend(chunk)

                # Drain as many full source-rate frames as the buffer
                # now holds. Each whole source frame resamples to
                # exactly ``samples_per_frame`` device samples, so the
                # rounding stays consistent across chunks regardless of
                # transport chunking.
                while len(buffer) >= bytes_per_src_frame:
                    src_frame = bytes(buffer[:bytes_per_src_frame])
                    del buffer[:bytes_per_src_frame]
                    if source_rate != DEVICE_SAMPLE_RATE:
                        pcm_frame = resample_pcm16_linear(
                            src_frame, source_rate, DEVICE_SAMPLE_RATE
                        )
                        # Resampler should produce exactly one device
                        # frame; pad / truncate defensively so the
                        # opus encoder gets the size it expects.
                        if len(pcm_frame) > bytes_per_frame:
                            pcm_frame = pcm_frame[:bytes_per_frame]
                        elif len(pcm_frame) < bytes_per_frame:
                            pcm_frame = pcm_frame + b"\x00" * (
                                bytes_per_frame - len(pcm_frame)
                            )
                    else:
                        pcm_frame = src_frame
                    try:
                        opus_frame = encoder.encode(
                            pcm_frame, samples_per_frame
                        )
                    except Exception as exc:
                        raise RuntimeError(
                            f"Opus encoding failed: {exc}"
                        ) from exc

                    if not await _push(opus_frame):
                        break  # device disconnected mid-stream

                if push_error is not None:
                    break

            # Stream ended cleanly: flush any trailing partial frame as
            # zero-padded audio so the last few milliseconds of speech
            # aren't silently dropped. We zero-pad in the source rate
            # space first (down to 16-bit sample alignment, then up to
            # one source-rate frame), then resample once to a device
            # frame, mirroring the per-frame logic above.
            if push_error is None and len(buffer) > 0:
                tail_src = bytes(buffer)
                if len(tail_src) % 2 != 0:
                    # Drop a stray byte rather than crash. The producer
                    # protocol expects 16-bit aligned PCM; a half sample
                    # at EOS has no defined interpretation.
                    tail_src = tail_src[:-1]
                if len(tail_src) > 0:
                    if len(tail_src) < bytes_per_src_frame:
                        tail_src = tail_src + b"\x00" * (
                            bytes_per_src_frame - len(tail_src)
                        )
                    if source_rate != DEVICE_SAMPLE_RATE:
                        tail = resample_pcm16_linear(
                            tail_src, source_rate, DEVICE_SAMPLE_RATE
                        )
                        if len(tail) > bytes_per_frame:
                            tail = tail[:bytes_per_frame]
                        elif len(tail) < bytes_per_frame:
                            tail = tail + b"\x00" * (
                                bytes_per_frame - len(tail)
                            )
                    else:
                        tail = tail_src
                    try:
                        opus_frame = encoder.encode(
                            tail, samples_per_frame
                        )
                    except Exception as exc:
                        raise RuntimeError(
                            f"Opus encoding failed: {exc}"
                        ) from exc
                    await _push(opus_frame)
        finally:
            try:
                await gateway.esp32.send_tts_state("stop")
            except ConnectionError:
                pass

    if push_error is not None:
        raise RuntimeError(
            f"Device disconnected after sending {sent} frames: {push_error}"
        ) from push_error

    duration_ms = sent * DEVICE_FRAME_DURATION_MS

    if sent == 0:
        logger.warning(
            "send_pcm_stream: source=%s yielded no audio (producer "
            "cancelled or empty stream)",
            source_label,
        )
    else:
        logger.info(
            "send_pcm_stream: source=%s frames=%d duration_ms=%d",
            source_label,
            sent,
            duration_ms,
        )

    return {
        "source": source_label,
        "frame_count": sent,
        "sample_rate": DEVICE_SAMPLE_RATE,
        "frame_duration_ms": DEVICE_FRAME_DURATION_MS,
        "duration_ms": duration_ms,
    }
