"""Tests for the gateway audio_stream helpers (Issue #70 PR2 / Issue #91)."""

from __future__ import annotations

import pytest

from stackchan_mcp.audio_stream import (
    handle_audio_frame,
    is_recording,
    push_opus_frames,
    recording_owner,
    start_recording,
    stop_recording,
)


class _FakeESP32:
    def __init__(self, *, fail_after: int | None = None) -> None:
        self.frames: list[bytes] = []
        self._fail_after = fail_after

    async def send_audio_frame(self, frame: bytes) -> None:
        if self._fail_after is not None and len(self.frames) >= self._fail_after:
            raise ConnectionError("simulated mid-stream disconnect")
        self.frames.append(frame)


@pytest.fixture(autouse=True)
def _cleanup_recording_slot():
    """Always release the module-level recording slot between tests."""
    yield
    if is_recording():
        stop_recording()


@pytest.mark.asyncio
async def test_handle_audio_frame_discards_when_no_recording():
    """Frames are discarded when no recording slot is open (Issue #91)."""
    assert not is_recording()
    # Should not raise; no buffer to grow.
    await handle_audio_frame(b"\x00\x01\x02", session_id="session-1")
    assert not is_recording()


@pytest.mark.asyncio
async def test_recording_lifecycle_buffers_frames_between_start_and_stop():
    """start_recording -> handle_audio_frame -> stop_recording returns the bytes.

    Outside the start/stop window, frames are silently discarded as
    before; inside it, the orchestrator collects them for the STT
    pipeline.
    """
    assert not is_recording()
    start_recording("session-listen")
    assert is_recording()

    await handle_audio_frame(b"frame-1", session_id="session-listen")
    await handle_audio_frame(b"frame-2", session_id="session-listen")
    await handle_audio_frame(b"frame-3", session_id="session-listen")

    frames = stop_recording()

    assert frames == [b"frame-1", b"frame-2", b"frame-3"]
    assert not is_recording()
    # A second stop returns an empty list rather than raising.
    assert stop_recording() == []


@pytest.mark.asyncio
async def test_handle_audio_frame_drops_frames_from_other_session():
    """Frames from a session other than the recording's are discarded.

    When ESP32 reconnects, ``ESP32Manager._handler`` swaps in a new
    connection and marks the old one disconnected, but the old
    socket's ``async for message in ws`` loop can still drain a
    binary frame or two before the close fully lands. Without
    session-id matching, those stale frames would land in the new
    session's recording buffer and corrupt the transcription.
    """
    start_recording("session-current")

    # Frame from the current session — buffered.
    await handle_audio_frame(b"current-frame", session_id="session-current")
    # Stale frame from the previous (now-disconnected) session.
    await handle_audio_frame(b"stale-frame", session_id="session-old")
    # Another current-session frame — still buffered.
    await handle_audio_frame(b"current-frame-2", session_id="session-current")

    frames = stop_recording()
    assert frames == [b"current-frame", b"current-frame-2"]


@pytest.mark.asyncio
async def test_start_recording_resets_previous_buffer():
    """Re-opening the slot drops any frames buffered from a leaked prior run.

    The listen_lock should prevent this in practice, but the audio
    pipeline should still be defensive — leaking frames from one
    capture into the next would mix transcriptions silently.
    """
    start_recording("session-a")
    await handle_audio_frame(b"leftover", session_id="session-a")

    # Without an intervening stop_recording (simulating a crashed
    # prior call), opening the slot afresh discards the leftover.
    start_recording("session-b")
    await handle_audio_frame(b"new-1", session_id="session-b")

    frames = stop_recording()
    assert frames == [b"new-1"]


@pytest.mark.asyncio
async def test_recording_hook_can_receive_frames_without_buffering():
    """Long-running modes can tap frames without growing the Opus buffer."""
    seen: list[bytes] = []

    start_recording(
        "session-hook",
        owner="beat_mode",
        frame_hook=seen.append,
        buffer_frames=False,
    )

    await handle_audio_frame(b"frame-1", session_id="session-hook")
    await handle_audio_frame(b"frame-2", session_id="session-hook")

    frames = stop_recording()
    assert seen == [b"frame-1", b"frame-2"]
    assert frames == []
    assert recording_owner() is None


@pytest.mark.asyncio
async def test_push_opus_frames_sends_each_frame_in_order():
    """All frames reach the device in the order they were given."""
    esp32 = _FakeESP32()
    frames = [b"a", b"b", b"c"]

    sent = await push_opus_frames(esp32, frames)

    assert sent == 3
    assert esp32.frames == frames


@pytest.mark.asyncio
async def test_push_opus_frames_propagates_disconnect():
    """A mid-stream ConnectionError surfaces, with the partial count visible."""
    esp32 = _FakeESP32(fail_after=2)
    frames = [b"a", b"b", b"c", b"d"]

    with pytest.raises(ConnectionError):
        await push_opus_frames(esp32, frames)

    # The first two frames did make it to the device before the failure.
    assert esp32.frames == [b"a", b"b"]


@pytest.mark.asyncio
async def test_push_opus_frames_empty_iterable_returns_zero():
    """Pushing zero frames is a no-op rather than an error."""
    esp32 = _FakeESP32()

    sent = await push_opus_frames(esp32, [])

    assert sent == 0
    assert esp32.frames == []
