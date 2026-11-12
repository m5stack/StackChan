"""Gateway-side beat mode lifecycle and motion/LED loop."""

from __future__ import annotations

import asyncio
import contextlib
from collections import deque
from dataclasses import dataclass, replace
from itertools import count
import json
import logging
import math
import os
import tempfile
import threading
from typing import Any
import wave

from ..audio_stream import (
    is_recording,
    recording_owner,
    start_recording,
    stop_recording_if_owner,
)
from ..stt.audio_utils import DEVICE_SAMPLE_RATE, StreamingOpusDecoder
from .tracker import BeatTracker

logger = logging.getLogger(__name__)

BEAT_MODE_OWNER = "beat_mode"
BEAT_MODE_OWNER_PREFIX = f"{BEAT_MODE_OWNER}:"
BASE_RING_LED_COUNT = 12
CAPTURE_SECONDS_DEFAULT = 30.0
OPUS_QUEUE_MAX_FRAMES = 200
LISTEN_STOP_TIMEOUT_S = 3.0
LISTEN_RESTART_AFTER_S = 2.5
LISTEN_RESTART_INITIAL_BACKOFF_S = 1.0
LISTEN_RESTART_MAX_BACKOFF_S = 8.0
MIN_MOTION_CONFIDENCE = 0.35
SERVO_YAW_MIN, SERVO_YAW_MAX = -90, 90
SERVO_PITCH_MIN, SERVO_PITCH_MAX = 5, 85
DEFAULT_SENSITIVITY = 0.5
MIN_ONSET_RMS_LEAST_SENSITIVE = 0.025
MIN_ONSET_RMS_DEFAULT = 0.004
MIN_ONSET_RMS_MOST_SENSITIVE = 0.001
_OWNER_GENERATIONS = count(1)


def _new_owner_token() -> str:
    return f"{BEAT_MODE_OWNER}:{next(_OWNER_GENERATIONS)}"


def is_beat_mode_owner(owner: str | None) -> bool:
    return owner == BEAT_MODE_OWNER or (
        owner is not None and owner.startswith(BEAT_MODE_OWNER_PREFIX)
    )


def _is_finite_number(value: Any) -> bool:
    if isinstance(value, bool) or not isinstance(value, int | float):
        return False
    try:
        return math.isfinite(float(value))
    except (OverflowError, ValueError):
        return False


def _clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def min_onset_rms_for_sensitivity(sensitivity: float) -> float:
    """Map 0..1 operator sensitivity to the tracker RMS floor on a log scale."""
    if not _is_finite_number(sensitivity) or not 0.0 <= float(sensitivity) <= 1.0:
        raise ValueError("sensitivity must be a number in 0..1")
    sensitivity = float(sensitivity)
    if sensitivity <= DEFAULT_SENSITIVITY:
        ratio = sensitivity / DEFAULT_SENSITIVITY
        return MIN_ONSET_RMS_LEAST_SENSITIVE * (
            MIN_ONSET_RMS_DEFAULT / MIN_ONSET_RMS_LEAST_SENSITIVE
        ) ** ratio
    ratio = (sensitivity - DEFAULT_SENSITIVITY) / (1.0 - DEFAULT_SENSITIVITY)
    return MIN_ONSET_RMS_DEFAULT * (
        MIN_ONSET_RMS_MOST_SENSITIVE / MIN_ONSET_RMS_DEFAULT
    ) ** ratio


def _validate_color(value: tuple[int, int, int]) -> tuple[int, int, int]:
    if len(value) != 3:
        raise ValueError("color must contain exactly three RGB channels")
    channels: list[int] = []
    for channel in value:
        if not isinstance(channel, int) or isinstance(channel, bool):
            raise ValueError("color channels must be integers in 0..255")
        if not 0 <= channel <= 255:
            raise ValueError("color channels must be integers in 0..255")
        channels.append(int(channel))
    return channels[0], channels[1], channels[2]


@dataclass(frozen=True)
class BeatModeConfig:
    motion_intensity: float = 0.5
    sensitivity: float = DEFAULT_SENSITIVITY
    color: tuple[int, int, int] = (0, 160, 255)
    duration_sec: int | None = None
    blink_rate: float = 1.0
    motion_enabled: bool = True
    led_enabled: bool = True
    capture_seconds: float = CAPTURE_SECONDS_DEFAULT

    def __post_init__(self) -> None:
        if not _is_finite_number(self.motion_intensity):
            raise ValueError("motion_intensity must be a number in 0..1")
        if not 0.0 <= float(self.motion_intensity) <= 1.0:
            raise ValueError("motion_intensity must be a number in 0..1")
        object.__setattr__(self, "motion_intensity", float(self.motion_intensity))

        if not _is_finite_number(self.sensitivity):
            raise ValueError("sensitivity must be a number in 0..1")
        if not 0.0 <= float(self.sensitivity) <= 1.0:
            raise ValueError("sensitivity must be a number in 0..1")
        object.__setattr__(self, "sensitivity", float(self.sensitivity))
        object.__setattr__(self, "color", _validate_color(self.color))

        if self.duration_sec is not None:
            if (
                not isinstance(self.duration_sec, int)
                or isinstance(self.duration_sec, bool)
                or self.duration_sec <= 0
            ):
                raise ValueError("duration_sec must be a positive integer or null")

        if not _is_finite_number(self.blink_rate):
            raise ValueError("blink_rate must be a number in 0.25..4")
        if not 0.25 <= float(self.blink_rate) <= 4.0:
            raise ValueError("blink_rate must be a number in 0.25..4")
        object.__setattr__(self, "blink_rate", float(self.blink_rate))

        if not isinstance(self.motion_enabled, bool):
            raise ValueError("motion_enabled must be a boolean")
        if not isinstance(self.led_enabled, bool):
            raise ValueError("led_enabled must be a boolean")

        if not _is_finite_number(self.capture_seconds) or self.capture_seconds <= 0:
            raise ValueError("capture_seconds must be > 0")
        object.__setattr__(self, "capture_seconds", float(self.capture_seconds))

    @property
    def min_onset_rms(self) -> float:
        return min_onset_rms_for_sensitivity(self.sensitivity)


class RollingPcmBuffer:
    def __init__(
        self,
        *,
        seconds: float,
        sample_rate: int = DEVICE_SAMPLE_RATE,
        bytes_per_sample: int = 2,
    ) -> None:
        self.sample_rate = sample_rate
        self.bytes_per_sample = bytes_per_sample
        self._max_bytes = int(seconds * sample_rate * bytes_per_sample)
        self._chunks: deque[bytes] = deque()
        self._total_bytes = 0
        self._lock = threading.RLock()

    @property
    def duration_seconds(self) -> float:
        with self._lock:
            return self._total_bytes / float(self.sample_rate * self.bytes_per_sample)

    def append(self, pcm: bytes) -> None:
        if not pcm:
            return
        usable = len(pcm) - (len(pcm) % self.bytes_per_sample)
        if usable <= 0:
            return
        chunk = pcm[:usable]
        with self._lock:
            self._chunks.append(chunk)
            self._total_bytes += len(chunk)
            self._trim_locked()

    def read_recent(self, seconds: float) -> tuple[bytes, float]:
        if not _is_finite_number(seconds) or seconds <= 0:
            raise ValueError("seconds must be a positive number")
        requested_bytes = int(seconds * self.sample_rate * self.bytes_per_sample)
        requested_bytes -= requested_bytes % self.bytes_per_sample
        if requested_bytes <= 0:
            raise ValueError("seconds is too small to include a PCM sample")
        with self._lock:
            data = b"".join(self._chunks)
        pcm = data[-requested_bytes:]
        actual_seconds = len(pcm) / float(self.sample_rate * self.bytes_per_sample)
        return pcm, actual_seconds

    def _trim_locked(self) -> None:
        while self._total_bytes > self._max_bytes and self._chunks:
            excess = self._total_bytes - self._max_bytes
            first = self._chunks[0]
            if len(first) <= excess:
                self._chunks.popleft()
                self._total_bytes -= len(first)
                continue
            trim = excess - (excess % self.bytes_per_sample)
            if trim <= 0:
                trim = self.bytes_per_sample
            self._chunks[0] = first[trim:]
            self._total_bytes -= trim


class BeatMode:
    def __init__(self, gateway: Any, config: BeatModeConfig) -> None:
        self._gateway = gateway
        self._config = config
        self._tracker = BeatTracker(
            sample_rate=DEVICE_SAMPLE_RATE,
            min_onset_rms=config.min_onset_rms,
        )
        self._capture = RollingPcmBuffer(seconds=config.capture_seconds)
        self._opus_queue: asyncio.Queue[bytes] = asyncio.Queue(
            maxsize=OPUS_QUEUE_MAX_FRAMES
        )
        self._recording_owner = _new_owner_token()
        self._decoder: StreamingOpusDecoder | None = None
        self._stop_event = asyncio.Event()
        self._tasks: list[asyncio.Task[None]] = []
        self._stop_lock = asyncio.Lock()
        self._stop_task: asyncio.Task[dict[str, Any]] | None = None
        self._active = False
        self._started_at: float | None = None
        self._stopped_at: float | None = None
        self._session_id: str | None = None
        self._capture_state = "stopped"
        self._last_error: str | None = None
        self._last_audio_at: float | None = None
        self._last_listen_start_at: float | None = None
        self._listen_start_count = 0
        self._frames_received = 0
        self._frames_decoded = 0
        self._frames_dropped = 0
        self._decode_errors = 0
        self._motion_commands = 0
        self._led_commands = 0
        self._watchdog_attempts = 0
        self._restart_backoff_s = LISTEN_RESTART_INITIAL_BACKOFF_S

    @property
    def active(self) -> bool:
        return self._active

    async def start(self) -> dict[str, Any]:
        if self._active:
            return self.status()

        self._ensure_decoder_available()
        self._ensure_device_ready()

        self._stop_event.clear()
        self._started_at = asyncio.get_running_loop().time()
        self._stopped_at = None
        self._capture_state = "starting"
        try:
            await self._arm_listen()
        except BaseException:
            self._release_recording_slot()
            self._capture_state = "stopped"
            self._stopped_at = asyncio.get_running_loop().time()
            raise

        self._active = True
        self._tasks = [
            asyncio.create_task(
                self._decode_loop(),
                name="stackchan-beat-mode-decode",
            ),
            asyncio.create_task(
                self._listen_watchdog_loop(),
                name="stackchan-beat-mode-listen-watchdog",
            ),
            asyncio.create_task(
                self._motion_led_loop(),
                name="stackchan-beat-mode-motion-led",
            ),
        ]
        if self._config.duration_sec is not None:
            self._tasks.append(
                asyncio.create_task(
                    self._auto_stop_after(self._config.duration_sec),
                    name="stackchan-beat-mode-auto-stop",
                )
            )
        return self.status()

    async def stop(
        self,
        *,
        listen_stop_timeout_s: float = LISTEN_STOP_TIMEOUT_S,
    ) -> dict[str, Any]:
        if self._stop_task is None:
            async with self._stop_lock:
                if self._stop_task is None:
                    if not self._active and self._capture_state == "stopped":
                        return self.status()
                    self._stop_task = asyncio.create_task(
                        self._stop_impl(
                            asyncio.current_task(),
                            listen_stop_timeout_s=listen_stop_timeout_s,
                        ),
                        name="stackchan-beat-mode-stop",
                    )
        return await asyncio.shield(self._stop_task)

    async def _stop_impl(
        self,
        caller: asyncio.Task[Any] | None,
        *,
        listen_stop_timeout_s: float,
    ) -> dict[str, Any]:
        self._capture_state = "stopping"
        self._stop_event.set()
        try:
            await self._bounded_listen_stop(listen_stop_timeout_s)
        finally:
            await self._finish_stop(caller)
        return self.status()

    async def _finish_stop(self, caller: asyncio.Task[Any] | None) -> None:
        self._release_recording_slot()

        current = asyncio.current_task()
        for task in self._tasks:
            if task not in (current, caller) and not task.done():
                task.cancel()
        for task in self._tasks:
            if task in (current, caller):
                continue
            try:
                await task
            except asyncio.CancelledError:
                pass
            except Exception as exc:  # pragma: no cover - defensive
                self._last_error = str(exc)
        self._tasks = []
        self._active = False
        self._capture_state = "stopped"
        self._stopped_at = asyncio.get_running_loop().time()

    async def update(
        self,
        *,
        motion_intensity: float | None = None,
        sensitivity: float | None = None,
        color: tuple[int, int, int] | None = None,
        blink_rate: float | None = None,
        motion_enabled: bool | None = None,
        led_enabled: bool | None = None,
    ) -> dict[str, Any]:
        updates: dict[str, Any] = {}
        if motion_intensity is not None:
            updates["motion_intensity"] = motion_intensity
        if sensitivity is not None:
            updates["sensitivity"] = sensitivity
        if color is not None:
            updates["color"] = color
        if blink_rate is not None:
            updates["blink_rate"] = blink_rate
        if motion_enabled is not None:
            updates["motion_enabled"] = motion_enabled
        if led_enabled is not None:
            updates["led_enabled"] = led_enabled
        self._config = replace(self._config, **updates)
        if "sensitivity" in updates:
            self._tracker.set_min_onset_rms(self._config.min_onset_rms)
        return self.status()

    def status(self) -> dict[str, Any]:
        loop_time = None
        try:
            loop_time = asyncio.get_running_loop().time()
        except RuntimeError:
            pass
        now = loop_time if loop_time is not None else self._last_audio_at

        beat = self._tracker.snapshot()
        last_beat_at_ms = (
            int(beat.last_beat_at * 1000) if beat.last_beat_at is not None else None
        )
        last_audio_at_ms = (
            int(self._last_audio_at * 1000)
            if self._last_audio_at is not None
            else None
        )
        last_beat_age_ms = (
            int((now - beat.last_beat_at) * 1000)
            if now is not None and beat.last_beat_at is not None
            else None
        )
        last_audio_age_ms = (
            int((now - self._last_audio_at) * 1000)
            if now is not None and self._last_audio_at is not None
            else None
        )
        capture_healthy = (
            self._active
            and self._capture_state == "listening"
            and self._last_audio_at is not None
            and now is not None
            and now - self._last_audio_at <= LISTEN_RESTART_AFTER_S
        )
        return {
            "active": self._active,
            "bpm": beat.bpm,
            "confidence": beat.confidence,
            "last_beat_at_ms": last_beat_at_ms,
            "last_beat_age_ms": last_beat_age_ms,
            "onsets": beat.onset_count,
            "sensitivity": self._config.sensitivity,
            "min_onset_rms": self._tracker.get_min_onset_rms(),
            "capture_healthy": capture_healthy,
            "capture_state": self._capture_state,
            "capture_seconds": round(self._capture.duration_seconds, 3),
            "capture_window_seconds": self._config.capture_seconds,
            "last_audio_at_ms": last_audio_at_ms,
            "last_audio_age_ms": last_audio_age_ms,
            "frames_received": self._frames_received,
            "frames_decoded": self._frames_decoded,
            "frames_dropped": self._frames_dropped,
            "decode_errors": self._decode_errors,
            "listen_start_count": self._listen_start_count,
            "watchdog_attempts": self._watchdog_attempts,
            "last_error": self._last_error,
            "session_id": self._session_id,
            "motion": {
                "enabled": self._config.motion_enabled,
                "intensity": self._config.motion_intensity,
                "commands_sent": self._motion_commands,
            },
            "led": {
                "enabled": self._config.led_enabled,
                "target": "base_ring",
                "color": list(self._config.color),
                "blink_rate": self._config.blink_rate,
                "commands_sent": self._led_commands,
            },
        }

    async def save_clip(self, seconds: float) -> dict[str, Any]:
        pcm, actual_seconds = self._capture.read_recent(seconds)
        if not pcm:
            raise RuntimeError("beat mode has no captured PCM audio yet")
        fd, path = tempfile.mkstemp(prefix="stackchan-beat-", suffix=".wav")
        try:
            with os.fdopen(fd, "wb") as fp:
                with wave.open(fp, "wb") as wav:
                    wav.setnchannels(1)
                    wav.setsampwidth(2)
                    wav.setframerate(DEVICE_SAMPLE_RATE)
                    wav.writeframes(pcm)
        except Exception:
            with contextlib.suppress(OSError):
                os.close(fd)
            with contextlib.suppress(OSError):
                os.unlink(path)
            raise
        return {
            "path": path,
            "seconds": round(actual_seconds, 3),
            "sample_rate": DEVICE_SAMPLE_RATE,
            "channels": 1,
            "sample_width_bytes": 2,
            "active": self._active,
        }

    def _ensure_decoder_available(self) -> None:
        self._decoder = StreamingOpusDecoder()

    def _ensure_device_ready(self) -> None:
        if not getattr(self._gateway.esp32, "device_connected", False):
            raise RuntimeError("No ESP32 device connected; cannot start beat mode.")
        connection = getattr(self._gateway.esp32, "connection", None)
        proto_version = getattr(connection, "protocol_version", 1)
        if proto_version != 1:
            raise RuntimeError(
                "beat mode requires WebSocket protocol v1 because the inbound "
                "audio path receives raw Opus frames"
            )

    async def _arm_listen(self) -> None:
        lock = getattr(self._gateway.esp32, "listen_lock", None)
        if lock is None:
            await self._arm_listen_unlocked()
            return
        async with lock:
            await self._arm_listen_unlocked()

    async def _arm_listen_unlocked(self) -> None:
        self._ensure_device_ready()
        owner = recording_owner()
        if is_recording() and owner != self._recording_owner:
            raise RuntimeError(
                "audio capture is already active; stop listen() or the "
                "device-driven capture before starting beat mode"
            )
        connection = self._gateway.esp32.connection
        session_id = getattr(connection, "session_id", "") if connection else ""
        self._reset_stream_if_session_changed(session_id)
        start_recording(
            session_id,
            owner=self._recording_owner,
            frame_hook=self._enqueue_opus_frame,
            buffer_frames=False,
        )
        await self._gateway.esp32.send_listen_state(
            "start",
            mode="manual",
            profile="raw",
        )
        self._session_id = session_id
        self._last_listen_start_at = asyncio.get_running_loop().time()
        self._listen_start_count += 1
        self._capture_state = "listening"

    async def _send_listen_stop(self) -> None:
        lock = getattr(self._gateway.esp32, "listen_lock", None)
        if lock is None:
            await self._gateway.esp32.send_listen_state("stop")
            return
        async with lock:
            await self._gateway.esp32.send_listen_state("stop")

    async def _bounded_listen_stop(self, timeout_s: float) -> None:
        task = asyncio.create_task(self._send_listen_stop())
        try:
            done, _ = await asyncio.wait({task}, timeout=timeout_s)
        except asyncio.CancelledError:
            task.cancel()
            task.add_done_callback(self._consume_listen_stop_result)
            raise
        if task not in done:
            task.cancel()
            self._last_error = f"best-effort listen.stop timed out after {timeout_s:.1f}s"
            logger.warning(
                "best-effort beat mode listen.stop timed out after %.1fs",
                timeout_s,
            )
            await asyncio.sleep(0)
            if task.done():
                self._consume_listen_stop_result(task)
            else:
                task.add_done_callback(self._consume_listen_stop_result)
            return
        try:
            await task
        except Exception as exc:
            self._last_error = f"best-effort listen.stop failed: {exc}"
            logger.warning("best-effort beat mode listen.stop failed: %s", exc)

    @staticmethod
    def _consume_listen_stop_result(task: asyncio.Task[None]) -> None:
        with contextlib.suppress(asyncio.CancelledError, Exception):
            task.result()

    def _release_recording_slot(self) -> None:
        stop_recording_if_owner(self._recording_owner)

    def _reset_stream_if_session_changed(self, session_id: str) -> None:
        if self._session_id is None or self._session_id == session_id:
            return
        dropped = self._discard_queued_opus_frames()
        self._ensure_decoder_available()
        if dropped:
            logger.debug(
                "beat mode discarded %d queued Opus frames after session changed "
                "from %s to %s",
                dropped,
                self._session_id,
                session_id,
            )

    def _discard_queued_opus_frames(self) -> int:
        dropped = 0
        while True:
            try:
                self._opus_queue.get_nowait()
            except asyncio.QueueEmpty:
                break
            dropped += 1
        self._frames_dropped += dropped
        return dropped

    def _enqueue_opus_frame(self, frame: bytes) -> None:
        self._frames_received += 1
        try:
            self._opus_queue.put_nowait(frame)
        except asyncio.QueueFull:
            try:
                self._opus_queue.get_nowait()
            except asyncio.QueueEmpty:
                pass
            try:
                self._opus_queue.put_nowait(frame)
            except asyncio.QueueFull:
                self._frames_dropped += 1
                return
            self._frames_dropped += 1

    async def _decode_loop(self) -> None:
        if self._decoder is None:
            self._decoder = StreamingOpusDecoder()
        while not self._stop_event.is_set():
            frame = await self._opus_queue.get()
            try:
                pcm = self._decoder.decode_frame(frame)
            except Exception as exc:
                self._decode_errors += 1
                self._frames_dropped += 1
                self._last_error = f"Opus decode failed: {exc}"
                self._capture_state = "decode_error"
                continue
            if not pcm:
                self._frames_dropped += 1
                continue
            now = asyncio.get_running_loop().time()
            self._capture.append(pcm)
            self._tracker.process_pcm(pcm, received_at=now)
            self._last_audio_at = now
            self._frames_decoded += 1
            self._capture_state = "listening"
            self._restart_backoff_s = LISTEN_RESTART_INITIAL_BACKOFF_S

    async def _listen_watchdog_loop(self) -> None:
        while not self._stop_event.is_set():
            await self._sleep_or_stop(1.0)
            if self._stop_event.is_set():
                break
            if not self._listen_restart_due(self._now()):
                continue

            self._capture_state = "retrying"
            self._watchdog_attempts += 1
            try:
                await self._arm_listen()
            except Exception as exc:
                self._last_error = f"listen.start retry failed: {exc}"
                await self._sleep_or_stop(self._restart_backoff_s)
                self._restart_backoff_s = min(
                    self._restart_backoff_s * 2.0,
                    LISTEN_RESTART_MAX_BACKOFF_S,
                )
            else:
                self._restart_backoff_s = LISTEN_RESTART_INITIAL_BACKOFF_S

    def _now(self) -> float:
        return asyncio.get_running_loop().time()

    def _listen_watchdog_reference(self) -> float | None:
        candidates = [
            value
            for value in (self._last_audio_at, self._last_listen_start_at)
            if value is not None
        ]
        return max(candidates) if candidates else None

    def _listen_restart_due(self, now: float) -> bool:
        reference = self._listen_watchdog_reference()
        return reference is None or now - reference >= LISTEN_RESTART_AFTER_S

    async def _motion_led_loop(self) -> None:
        loop = asyncio.get_running_loop()
        next_motion_at = loop.time()
        next_led_at = next_motion_at
        side = 1

        while not self._stop_event.is_set():
            beat = self._tracker.snapshot()
            if beat.bpm is None or beat.confidence < MIN_MOTION_CONFIDENCE:
                next_motion_at = loop.time()
                next_led_at = next_motion_at
                await self._sleep_or_stop(0.2)
                continue

            cfg = self._config
            beat_period = max(0.3, 60.0 / beat.bpm)
            led_period = beat_period / cfg.blink_rate
            now = loop.time()
            work: list[asyncio.Future[Any] | asyncio.Task[Any]] = []

            if cfg.motion_enabled and now >= next_motion_at:
                side *= -1
                work.append(asyncio.create_task(self._send_motion(side, cfg)))
                while next_motion_at <= now:
                    next_motion_at += beat_period
            elif not cfg.motion_enabled:
                next_motion_at = now + beat_period

            if cfg.led_enabled and now >= next_led_at:
                work.append(asyncio.create_task(self._flash_led(cfg, beat_period)))
                while next_led_at <= now:
                    next_led_at += led_period
            elif not cfg.led_enabled:
                next_led_at = now + led_period

            if work:
                await asyncio.gather(*work)
                continue

            sleep_until = min(next_motion_at, next_led_at)
            await self._sleep_or_stop(max(0.02, min(0.2, sleep_until - now)))

    async def _send_motion(self, side: int, cfg: BeatModeConfig) -> None:
        intensity = cfg.motion_intensity
        yaw = int(round(_clamp(side * 14.0 * intensity, SERVO_YAW_MIN, SERVO_YAW_MAX)))
        pitch = int(round(_clamp(45.0 + 4.0 * intensity, SERVO_PITCH_MIN, SERVO_PITCH_MAX)))
        speed_dps = int(round(_clamp(120.0 + 120.0 * intensity, 1.0, 240.0)))
        ok = await self._call_tool(
            "self.robot.set_head_angles",
            {"yaw": yaw, "pitch": pitch, "speed_dps": speed_dps},
        )
        if ok:
            self._motion_commands += 1

    async def _flash_led(self, cfg: BeatModeConfig, beat_period: float) -> None:
        color = cfg.color
        dim = tuple(int(channel * 0.12) for channel in color)
        flash_s = min(0.09, max(0.03, beat_period * 0.18))
        if await self._set_base_ring_color(color):
            await self._sleep_or_stop(flash_s)
            await self._set_base_ring_color(dim)

    async def _set_base_ring_color(self, color: tuple[int, int, int]) -> bool:
        colors = [list(color) for _ in range(BASE_RING_LED_COUNT)]
        ok = await self._call_tool(
            "self.led.set_many",
            {"colors": json.dumps(colors)},
        )
        if ok:
            self._led_commands += 1
        return ok

    async def _call_tool(self, name: str, arguments: dict[str, Any]) -> bool:
        try:
            result, error = await self._gateway.esp32.call_tool(name, arguments)
        except Exception as exc:
            self._last_error = f"{name} failed: {exc}"
            return False
        if error:
            if isinstance(error, dict):
                self._last_error = str(error.get("message", error))
            else:
                self._last_error = str(error)
            return False
        if isinstance(result, dict):
            if result.get("isError"):
                self._last_error = f"{name} returned isError"
                return False
            payload = self._decode_call_result_payload(result)
            if isinstance(payload, dict):
                if payload.get("isError"):
                    self._last_error = f"{name} payload returned isError"
                    return False
                if payload.get("ok") is False:
                    self._last_error = f"{name} payload reported ok=false"
                    return False
        return True

    @staticmethod
    def _decode_call_result_payload(result: dict[str, Any]) -> dict[str, Any] | None:
        if "content" not in result:
            return result
        content = result.get("content")
        if not isinstance(content, list) or not content:
            return None
        first = content[0]
        if not isinstance(first, dict):
            return None
        text = first.get("text")
        if not isinstance(text, str):
            return None
        try:
            payload = json.loads(text)
        except (TypeError, ValueError):
            return None
        return payload if isinstance(payload, dict) else None

    async def _auto_stop_after(self, duration_sec: int) -> None:
        await self._sleep_or_stop(float(duration_sec))
        if not self._stop_event.is_set():
            await _stop_mode_instance(self)

    async def _sleep_or_stop(self, delay: float) -> None:
        if delay <= 0:
            await asyncio.sleep(0)
            return
        try:
            await asyncio.wait_for(self._stop_event.wait(), timeout=delay)
        except asyncio.TimeoutError:
            pass


_mode: BeatMode | None = None
_mode_lock: asyncio.Lock | None = None


def _get_mode_lock() -> asyncio.Lock:
    global _mode_lock
    if _mode_lock is None:
        _mode_lock = asyncio.Lock()
    return _mode_lock


async def _stop_mode_instance(
    mode: BeatMode,
    *,
    listen_stop_timeout_s: float = LISTEN_STOP_TIMEOUT_S,
) -> dict[str, Any]:
    async with _get_mode_lock():
        if _mode is not mode:
            return mode.status()
        return await mode.stop(listen_stop_timeout_s=listen_stop_timeout_s)


async def start_beat_mode(gateway: Any, config: BeatModeConfig) -> dict[str, Any]:
    global _mode
    async with _get_mode_lock():
        if _mode is not None and _mode.active:
            await _mode.update(
                motion_intensity=config.motion_intensity,
                color=config.color,
                blink_rate=config.blink_rate,
                sensitivity=config.sensitivity,
                motion_enabled=config.motion_enabled,
                led_enabled=config.led_enabled,
            )
            return _mode.status()

        mode = BeatMode(gateway, config)
        _mode = mode
        try:
            return await mode.start()
        except BaseException:
            if _mode is mode:
                _mode = None
            raise


async def stop_beat_mode(
    *,
    listen_stop_timeout_s: float = LISTEN_STOP_TIMEOUT_S,
) -> dict[str, Any]:
    async with _get_mode_lock():
        if _mode is None:
            return {"active": False}
        return await _mode.stop(listen_stop_timeout_s=listen_stop_timeout_s)


async def update_beat_mode(
    *,
    motion_intensity: float | None = None,
    sensitivity: float | None = None,
    color: tuple[int, int, int] | None = None,
    blink_rate: float | None = None,
    motion_enabled: bool | None = None,
    led_enabled: bool | None = None,
) -> dict[str, Any]:
    async with _get_mode_lock():
        if _mode is None or not _mode.active:
            raise RuntimeError("beat mode is not active")
        return await _mode.update(
            motion_intensity=motion_intensity,
            sensitivity=sensitivity,
            color=color,
            blink_rate=blink_rate,
            motion_enabled=motion_enabled,
            led_enabled=led_enabled,
        )


def get_beat_mode_snapshot() -> dict[str, Any]:
    if _mode is None:
        return {"active": False}
    return _mode.status()


async def save_beat_clip(seconds: float) -> dict[str, Any]:
    if _mode is None:
        raise RuntimeError("beat mode has not captured audio in this gateway run")
    return await _mode.save_clip(seconds)
