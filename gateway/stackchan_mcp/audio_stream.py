"""Opus audio frame handling for the gateway <-> device link.

Outbound (TTS) frames are produced by
:mod:`stackchan_mcp.tts.audio_utils` and pushed here to the connected
ESP32 via :meth:`stackchan_mcp.esp32_client.ESP32Manager.send_audio_frame`.

The inbound side (STT pipeline, Phase 4 / Issue #91) is now wired:
binary frames coming up from the device land in
:func:`handle_audio_frame`, which buffers them into a module-level
recording slot when one is active. The
:mod:`stackchan_mcp.stt.orchestrator` opens the slot via
:func:`start_recording` before sending ``listen.start`` to the device
and closes it via :func:`stop_recording` after the capture window;
outside an active recording, inbound frames are still discarded.

The recording slot is intentionally a module-level singleton shared by
every connected device. MCP ``listen()`` calls on different devices are
serialised through :func:`capture_lock` (a gateway-wide lock, in addition
to each device's own listen lock): a second device's ``listen()`` waits
for the first capture to finish instead of running in parallel or
failing. Device-driven captures do not queue; they are declined while the
slot is held.
"""

from __future__ import annotations

import asyncio
import inspect
import logging
from collections.abc import Awaitable, Callable
from typing import TYPE_CHECKING, Iterable

if TYPE_CHECKING:
    from .esp32_client import ESP32Manager

logger = logging.getLogger(__name__)


# --- Recording slot (inbound STT capture) ---------------------------------
#
# A single capture at a time is enforced by the orchestrator's
# ``listen_lock``; this module only owns the buffer itself.

_recording_session_id: str | None = None
_recording_owner: str | None = None
_recording_frames: list[bytes] = []
_recording_frame_hook: Callable[[bytes], Awaitable[None] | None] | None = None
_recording_buffer_frames = True


_capture_lock: asyncio.Lock | None = None
_capture_lock_loop: asyncio.AbstractEventLoop | None = None


def capture_lock() -> asyncio.Lock:
    """Return the gateway-wide lock that serialises MCP listen captures."""
    global _capture_lock, _capture_lock_loop
    loop = asyncio.get_running_loop()
    if _capture_lock is None or _capture_lock_loop is not loop:
        _capture_lock = asyncio.Lock()
        _capture_lock_loop = loop
    return _capture_lock


def start_recording(
    session_id: str,
    *,
    owner: str = "listen",
    frame_hook: Callable[[bytes], Awaitable[None] | None] | None = None,
    buffer_frames: bool = True,
) -> None:
    """Open a fresh recording slot for ``session_id``.

    Any frames already buffered are discarded so a previous call that
    crashed before ``stop_recording`` cannot leak into the next
    capture. The orchestrator wraps start/stop in a try/finally to
    guarantee the slot is closed even on error.
    """
    global _recording_session_id, _recording_owner
    global _recording_frames, _recording_frame_hook, _recording_buffer_frames
    if _recording_session_id is not None:
        # Defensive: the lock should prevent this, but if it ever
        # fires we leak no audio — just log loudly so the regression
        # is visible.
        logger.warning(
            "start_recording called while owner=%s session=%s was still "
            "active; dropping %d buffered frames",
            _recording_owner,
            _recording_session_id,
            len(_recording_frames),
        )
    _recording_session_id = session_id
    _recording_owner = owner
    _recording_frames = []
    _recording_frame_hook = frame_hook
    _recording_buffer_frames = buffer_frames


def stop_recording() -> list[bytes]:
    """Close the recording slot and return the buffered Opus frames.

    Returns an empty list if no recording was active. The slot is
    cleared whether or not frames were captured so the next call to
    :func:`start_recording` starts clean.
    """
    global _recording_session_id, _recording_owner
    global _recording_frames, _recording_frame_hook, _recording_buffer_frames
    frames = _recording_frames
    _recording_session_id = None
    _recording_owner = None
    _recording_frames = []
    _recording_frame_hook = None
    _recording_buffer_frames = True
    return frames


def stop_recording_if_owner(owner: str) -> list[bytes]:
    """Close the recording slot only if it is held by ``owner``."""
    if _recording_owner != owner:
        return []
    return stop_recording()


def is_recording() -> bool:
    """Return ``True`` when a recording slot is currently open."""
    return _recording_session_id is not None


def recording_owner() -> str | None:
    """Return the owner label for the active recording slot, if any."""
    return _recording_owner


def is_recording_session(session_id: str) -> bool:
    """Return ``True`` when the recording slot belongs to ``session_id``.

    Used by per-session disconnect cleanup paths to confirm they still
    own the recording before tearing it down. A stale handler whose
    session was replaced by a fresh reconnection (or by an MCP-driven
    ``listen()``) must not clear the active buffer.
    """
    return _recording_session_id == session_id


async def handle_audio_frame(data: bytes, session_id: str) -> None:
    """Process an incoming binary Opus frame from the device.

    When a recording slot is active (see :func:`start_recording`) AND
    the frame belongs to the recording's session, appends the frame
    to the in-memory buffer for later decoding by the STT
    orchestrator. Frames from a different session — typical during
    a connection swap, where the old WebSocket handler is still
    draining incoming bytes after :meth:`ESP32Connection.disconnect`
    has been called on the main task — are dropped so they cannot
    bleed into the new connection's capture buffer.

    Outside of an active recording the frame is logged at debug
    level and discarded; the device may emit audio on its own (e.g.
    after an autonomous wake-word detection) and the gateway has no
    STT pipeline running for those frames yet.
    """
    if _recording_session_id is None:
        logger.debug(
            "audio_frame session=%s bytes=%d (discarded — no active recording)",
            session_id,
            len(data),
        )
        return
    if _recording_session_id != session_id:
        # A different connection is sending audio while a recording
        # for this session is in flight. This happens when ESP32
        # reconnects: ``ESP32Manager._handler`` swaps in a new
        # ``ESP32Connection`` and marks the old one disconnected,
        # but the old socket's ``async for message in ws`` loop can
        # still drain a frame or two before the close lands. Letting
        # those into the buffer would corrupt the new session's
        # transcription, so drop them here.
        logger.debug(
            "audio_frame session=%s bytes=%d (discarded — does not match "
            "recording session=%s)",
            session_id,
            len(data),
            _recording_session_id,
        )
        return
    if _recording_buffer_frames:
        _recording_frames.append(data)
    if _recording_frame_hook is not None:
        try:
            result = _recording_frame_hook(data)
            if inspect.isawaitable(result):
                await result
        except Exception as exc:  # pragma: no cover - defensive guard
            logger.warning("audio frame hook failed: %s", exc)
    action = "buffered" if _recording_buffer_frames else "delivered"
    logger.debug(
        "audio_frame session=%s bytes=%d %s (recording active)",
        session_id,
        len(data),
        action,
    )


async def push_opus_frames(
    esp32: ESP32Manager,
    frames: Iterable[bytes],
) -> int:
    """Push Opus frames to the connected ESP32.

    Returns the number of frames sent so the caller can report this to
    the MCP client. Raises :class:`ConnectionError` (via
    :meth:`ESP32Manager.send_audio_frame`) if the device disconnects
    mid-stream — the orchestrator turns that into a clean MCP error
    rather than letting it bubble up as a stack trace.
    """
    sent = 0
    for frame in frames:
        await esp32.send_audio_frame(frame)
        sent += 1
    return sent
