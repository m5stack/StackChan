"""Pose-stream subscriber that drives head servos from WebSocket frames."""

from __future__ import annotations

import asyncio
from collections import deque
from dataclasses import dataclass
import json
import math
from typing import Any, Optional

import websockets

from .wifi_power_save import (
    WIFI_PS_IDLE_MODE as WIFI_PS_IDLE_MODE,
    WIFI_PS_STREAM_MODE as WIFI_PS_STREAM_MODE,
    acquire_wifi_power_save,
    extract_wifi_power_save_result,
    reapply_wifi_power_save,
    release_wifi_power_save,
)

SERVO_YAW_MIN, SERVO_YAW_MAX = -90, 90
SERVO_PITCH_MIN, SERVO_PITCH_MAX = 5, 85
SERVO_MAX_SPEED_DPS = 240


def _clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def _is_finite_number(value: Any) -> bool:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return False
    try:
        return math.isfinite(float(value))
    except (OverflowError, ValueError):
        return False


@dataclass
class FollowPoseStreamConfig:
    url: str
    source_filter: Optional[str] = None
    frame_filter: Optional[str] = None
    flip_yaw: int = 1
    flip_pitch: int = 1
    pitch_center_deg: int = 45
    downsample_hz: float = 20.0
    max_step_deg: float = 12.0
    speed_dps: int = 240
    smoothing_window: int = 5
    seed_from_device: bool = True
    reconnect_initial_backoff_s: float = 1.5
    reconnect_max_backoff_s: float = 30.0

    def __post_init__(self) -> None:
        if (
            not isinstance(self.flip_yaw, int)
            or isinstance(self.flip_yaw, bool)
            or self.flip_yaw not in (-1, 1)
        ):
            raise ValueError("flip_yaw must be -1 or 1")
        if (
            not isinstance(self.flip_pitch, int)
            or isinstance(self.flip_pitch, bool)
            or self.flip_pitch not in (-1, 1)
        ):
            raise ValueError("flip_pitch must be -1 or 1")
        if (
            not isinstance(self.smoothing_window, int)
            or isinstance(self.smoothing_window, bool)
            or self.smoothing_window < 1
        ):
            raise ValueError("smoothing_window must be an integer >= 1")
        if self.downsample_hz <= 0:
            raise ValueError("downsample_hz must be > 0")
        if self.max_step_deg <= 0:
            raise ValueError("max_step_deg must be > 0")
        if self.reconnect_initial_backoff_s <= 0:
            raise ValueError("reconnect_initial_backoff_s must be > 0")
        if self.reconnect_max_backoff_s <= 0:
            raise ValueError("reconnect_max_backoff_s must be > 0")


def map_sensor_to_servo(
    sensor_yaw: float,
    sensor_pitch: float,
    *,
    flip_yaw: int,
    flip_pitch: int,
    pitch_center_deg: int,
) -> tuple[int, int]:
    servo_yaw = int(
        round(_clamp(sensor_yaw * flip_yaw, SERVO_YAW_MIN, SERVO_YAW_MAX))
    )
    servo_pitch = int(
        round(
            _clamp(
                pitch_center_deg + sensor_pitch * flip_pitch,
                SERVO_PITCH_MIN,
                SERVO_PITCH_MAX,
            )
        )
    )
    return servo_yaw, servo_pitch


def step_clamp(target: float, last: float, max_step_deg: float) -> float:
    return _clamp(target, last - max_step_deg, last + max_step_deg)


class FollowPoseStream:
    def __init__(self, gateway: Any, cfg: FollowPoseStreamConfig) -> None:
        self._gateway = gateway
        self._cfg = cfg
        self._stop_event = asyncio.Event()
        self._task: asyncio.Task[None] | None = None
        self._connect_state = "init"
        self._frames_received = 0
        self._frames_accepted = 0
        self._commands_sent = 0
        self._last_frame_ts: int | None = None
        self._last_error: str | None = None
        self._last_servo_yaw = 0
        self._last_servo_pitch = int(
            round(_clamp(cfg.pitch_center_deg, SERVO_PITCH_MIN, SERVO_PITCH_MAX))
        )
        self._initial_pose_seeded = False
        self._last_sent_at: float | None = None
        self._samples: deque[tuple[float, float]] = deque(
            maxlen=cfg.smoothing_window
        )
        self._wifi_ps_apply_result: dict[str, Any] | None = None
        self._wifi_ps_restore_result: dict[str, Any] | None = None
        self._wifi_ps_previous: str | None = None

    @property
    def url(self) -> str:
        return self._cfg.url

    def status(self) -> dict[str, Any]:
        running = self._task is not None and not self._task.done()
        return {
            "running": running,
            "url": self._cfg.url,
            "source_filter": self._cfg.source_filter,
            "frame_filter": self._cfg.frame_filter,
            "connect_state": self._connect_state,
            "frames_received": self._frames_received,
            "frames_accepted": self._frames_accepted,
            "commands_sent": self._commands_sent,
            "last_frame_ts": self._last_frame_ts,
            "last_error": self._last_error,
            "wifi_ps_apply_result": self._wifi_ps_apply_result,
            "wifi_ps_restore_result": self._wifi_ps_restore_result,
            "wifi_ps_previous": self._wifi_ps_previous,
            "last_servo": {
                "yaw": self._last_servo_yaw,
                "pitch": self._last_servo_pitch,
            },
        }

    async def start(self) -> None:
        if self._task is not None and not self._task.done():
            return
        self._stop_event.clear()
        self._connect_state = "init"
        self._wifi_ps_previous = None
        self._wifi_ps_apply_result = await acquire_wifi_power_save(
            self._gateway.esp32
        )
        self._record_wifi_ps_previous(self._wifi_ps_apply_result)
        self._task = asyncio.create_task(
            self._run(),
            name="stackchan-follow-pose-stream",
        )

    async def stop(self) -> None:
        self._stop_event.set()
        task = self._task
        if task is not None and not task.done():
            task.cancel()
        try:
            if task is not None:
                try:
                    await task
                except asyncio.CancelledError:
                    pass
                except Exception as exc:  # pragma: no cover - defensive
                    self._last_error = str(exc)
        finally:
            self._wifi_ps_restore_result = await release_wifi_power_save(
                self._gateway.esp32
            )
            self._connect_state = "stopped"

    def _record_wifi_ps_previous(self, result: Any) -> None:
        if not isinstance(result, dict):
            return
        previous = result.get("previous")
        if isinstance(previous, str) and previous and previous != "unknown":
            self._wifi_ps_previous = previous

    def _invalidate_device_state(self) -> None:
        """F6: forget cached device state after a transport / connect
        error so the next reachable frame re-seeds from the live head
        pose and re-applies WiFi PS=none. Used when the device is
        suspected to have disconnected or rebooted mid-stream.
        """
        if self._cfg.seed_from_device:
            self._initial_pose_seeded = False
        if isinstance(self._wifi_ps_apply_result, dict):
            self._wifi_ps_apply_result = {
                **self._wifi_ps_apply_result,
                "ok": False,
            }

    @staticmethod
    def _is_device_disconnect_error(error: Any) -> bool:
        """Heuristic for the gateway-side error envelope that signals
        the ESP32 is not currently reachable (vs. servo-bus or
        validation errors that should not invalidate the cached pose).
        """
        if not isinstance(error, dict):
            return False
        message = error.get("message")
        if not isinstance(message, str):
            return False
        lowered = message.lower()
        return (
            "not connected" in lowered
            or "not initialized" in lowered
            or "device" in lowered and "connect" in lowered
        )

    async def _maybe_reapply_wifi_ps(self) -> None:
        """Retry the start-time WiFi PS apply if it failed.

        Used by both _run() (initial seed success path) and _consume()
        (in-stream seed retry path). If the start-time apply already
        succeeded, this is a no-op; otherwise the stream mode is sent
        again now that the device is reachable, and the apply_result /
        previous fields are refreshed.
        """
        if (
            isinstance(self._wifi_ps_apply_result, dict)
            and self._wifi_ps_apply_result.get("ok")
        ):
            return
        self._wifi_ps_apply_result = await reapply_wifi_power_save(
            self._gateway.esp32
        )
        self._record_wifi_ps_previous(self._wifi_ps_apply_result)

    @staticmethod
    def _extract_wifi_ps_result(result: Any) -> dict[str, Any]:
        return extract_wifi_power_save_result(result)

    async def _run(self) -> None:
        backoff = self._cfg.reconnect_initial_backoff_s
        try:
            while not self._stop_event.is_set():
                try:
                    self._connect_state = "connecting"
                    async with websockets.connect(self._cfg.url) as ws:
                        self._connect_state = "connected"
                        backoff = self._cfg.reconnect_initial_backoff_s
                        if self._cfg.seed_from_device and not self._initial_pose_seeded:
                            seeded_now = await self._seed_from_device()
                            if seeded_now:
                                # F4: device is reachable for the first time
                                # in this stream; retry the WiFi PS apply if
                                # it failed at start time.
                                await self._maybe_reapply_wifi_ps()
                        await self._consume(ws)
                except asyncio.CancelledError:
                    raise
                except Exception as exc:
                    if self._stop_event.is_set():
                        break
                    self._last_error = str(exc)
                    self._connect_state = "reconnecting"

                if self._stop_event.is_set():
                    break

                self._connect_state = "reconnecting"
                try:
                    await asyncio.wait_for(
                        self._stop_event.wait(),
                        timeout=backoff,
                    )
                except asyncio.TimeoutError:
                    pass
                backoff = min(backoff * 2, self._cfg.reconnect_max_backoff_s)
        finally:
            self._connect_state = "stopped"

    async def _seed_from_device(self) -> bool:
        try:
            result, error = await self._gateway.esp32.call_tool(
                "self.robot.get_head_angles",
                {},
            )
        except Exception as exc:
            self._last_error = str(exc)
            return False

        if error:
            if isinstance(error, dict):
                self._last_error = str(error.get("message", error))
            else:
                self._last_error = str(error)
            return False

        if not isinstance(result, dict):
            return False

        if result.get("isError"):
            self._last_error = "self.robot.get_head_angles returned isError"
            return False

        angles = self._extract_head_angles(result)
        if angles is None:
            return False

        yaw, pitch = angles
        if not (_is_finite_number(yaw) and _is_finite_number(pitch)):
            return False
        self._last_servo_yaw = int(
            round(_clamp(float(yaw), SERVO_YAW_MIN, SERVO_YAW_MAX))
        )
        self._last_servo_pitch = int(
            round(_clamp(float(pitch), SERVO_PITCH_MIN, SERVO_PITCH_MAX))
        )
        self._initial_pose_seeded = True
        return True

    @staticmethod
    def _decode_call_result_payload(result: dict[str, Any]) -> Optional[dict[str, Any]]:
        """Decode a firmware CallToolResult or return a plain dict fallback."""
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
        if not isinstance(payload, dict):
            return None
        return payload

    @staticmethod
    def _extract_head_angles(result: Any) -> Optional[tuple[Any, Any]]:
        """Extract yaw/pitch from a get_head_angles CallToolResult.

        The MCP transport wraps tool replies as
        ``{"content": [{"text": "<json>"}], "isError": ...}`` where the
        text is a JSON-encoded payload from the firmware side. Also
        tolerate a plain ``{"yaw": ..., "pitch": ...}`` dict so compact
        test stubs keep working. Returns ``None`` if the shape is not
        recognised so the caller keeps the seed defaults.
        """
        if not isinstance(result, dict):
            return None
        if "yaw" in result or "pitch" in result:
            return result.get("yaw"), result.get("pitch")

        payload = FollowPoseStream._decode_call_result_payload(result)
        if payload is None or ("yaw" not in payload and "pitch" not in payload):
            return None
        return payload.get("yaw"), payload.get("pitch")

    async def _consume(self, ws: Any) -> None:
        async for msg in ws:
            if self._stop_event.is_set():
                break
            self._frames_received += 1
            try:
                frame = json.loads(msg)
            except (TypeError, ValueError):
                continue
            if not isinstance(frame, dict):
                continue
            if (
                self._cfg.source_filter is not None
                and frame.get("source") != self._cfg.source_filter
            ):
                continue
            if (
                self._cfg.frame_filter is not None
                and frame.get("frame") != self._cfg.frame_filter
            ):
                continue

            yaw = frame.get("yaw")
            pitch = frame.get("pitch")
            if not _is_finite_number(yaw) or not _is_finite_number(pitch):
                continue

            self._frames_accepted += 1
            ts = frame.get("ts")
            if _is_finite_number(ts):
                self._last_frame_ts = int(ts)

            if self._cfg.seed_from_device and not self._initial_pose_seeded:
                seeded = await self._seed_from_device()
                if not seeded:
                    continue
                # F3 fix: if the start-time WiFi PS apply failed (e.g.
                # ESP32 was disconnected at start), retry it now that
                # the device is reachable. Otherwise the ~800 ms DTIM
                # send-jitter this tool exists to avoid would persist
                # for the rest of the stream. F4 mirrors the same gate
                # on the _run() initial-seed success path.
                await self._maybe_reapply_wifi_ps()

            self._samples.append((float(yaw), float(pitch)))
            loop = asyncio.get_running_loop()
            now = loop.time()
            if (
                self._last_sent_at is not None
                and now - self._last_sent_at < 1.0 / self._cfg.downsample_hz
            ):
                continue

            avg_yaw = sum(sample[0] for sample in self._samples) / len(
                self._samples
            )
            avg_pitch = sum(sample[1] for sample in self._samples) / len(
                self._samples
            )
            target_yaw, target_pitch = map_sensor_to_servo(
                avg_yaw,
                avg_pitch,
                flip_yaw=self._cfg.flip_yaw,
                flip_pitch=self._cfg.flip_pitch,
                pitch_center_deg=self._cfg.pitch_center_deg,
            )
            stepped_yaw = step_clamp(
                target_yaw,
                self._last_servo_yaw,
                self._cfg.max_step_deg,
            )
            stepped_pitch = step_clamp(
                target_pitch,
                self._last_servo_pitch,
                self._cfg.max_step_deg,
            )
            servo_yaw = int(
                round(_clamp(stepped_yaw, SERVO_YAW_MIN, SERVO_YAW_MAX))
            )
            servo_pitch = int(
                round(_clamp(stepped_pitch, SERVO_PITCH_MIN, SERVO_PITCH_MAX))
            )
            speed_dps = int(_clamp(self._cfg.speed_dps, 1, SERVO_MAX_SPEED_DPS))

            try:
                result, error = await self._gateway.esp32.call_tool(
                    "self.robot.set_head_angles",
                    {
                        "yaw": servo_yaw,
                        "pitch": servo_pitch,
                        "speed_dps": speed_dps,
                    },
                )
            except Exception as exc:
                self._last_error = str(exc)
                # F6: device transport raised — assume the ESP32 is no
                # longer reachable. Invalidate seed + WiFi PS state so
                # the next frame that lands re-seeds from the device
                # and reapplies WiFi PS to "none".
                self._invalidate_device_state()
                continue

            if error:
                if isinstance(error, dict):
                    self._last_error = str(error.get("message", error))
                else:
                    self._last_error = str(error)
                # F6: device-connect errors mean the cached seed and
                # WiFi PS state are stale relative to whatever the
                # device booted into; reset both so the next reachable
                # frame re-seeds before issuing a swing-prone command.
                if self._is_device_disconnect_error(error):
                    self._invalidate_device_state()
                continue

            if isinstance(result, dict):
                if result.get("isError"):
                    self._last_error = "set_head_angles reported isError"
                    continue
                payload = self._decode_call_result_payload(result)
                if isinstance(payload, dict):
                    if payload.get("isError"):
                        self._last_error = (
                            "set_head_angles payload reported isError"
                        )
                        continue
                    if payload.get("ok") is False:
                        self._last_error = "set_head_angles payload reported ok=false"
                        continue
                    if payload.get("servo_init_ok") is False:
                        self._last_error = (
                            "set_head_angles payload reported "
                            "servo_init_ok=false"
                        )
                        continue
                    if payload.get("servo_ok") is False:
                        self._last_error = (
                            "set_head_angles payload reported servo_ok=false"
                        )
                        continue

            self._last_sent_at = now
            self._last_servo_yaw = servo_yaw
            self._last_servo_pitch = servo_pitch
            self._commands_sent += 1


_follower: Optional[FollowPoseStream] = None
# F5: serialise start_follow / stop_follow so an interleaving stop
# cannot clear _follower while start is mid-await, leaving start's
# final status() call to dereference None. get_follow_status() is
# intentionally lock-free (it snapshots _follower into a local).
_follower_lock: Optional[asyncio.Lock] = None


def _get_follower_lock() -> asyncio.Lock:
    global _follower_lock
    if _follower_lock is None:
        _follower_lock = asyncio.Lock()
    return _follower_lock


async def start_follow(gateway: Any, cfg: FollowPoseStreamConfig) -> dict[str, Any]:
    """Cancel previous follower if running, then start a new one.

    Returns the initial status snapshot.
    """
    global _follower
    async with _get_follower_lock():
        if _follower is not None:
            await _follower.stop()
        follower = FollowPoseStream(gateway, cfg)
        _follower = follower
        await follower.start()
        # Use the local `follower` reference rather than _follower so a
        # racing stop_follow() that cleared _follower cannot make this
        # final status() dereference None.
        return follower.status()


async def stop_follow() -> dict[str, Any]:
    """Cancel and clear the singleton; return the final status."""
    global _follower
    async with _get_follower_lock():
        if _follower is None:
            return {"running": False}
        follower = _follower
        await follower.stop()
        status = follower.status()
        status["running"] = False
        _follower = None
        return status


def get_follow_status() -> dict[str, Any]:
    """Snapshot; returns {"running": False} if no follower is registered."""
    follower = _follower
    if follower is None:
        return {"running": False}
    return follower.status()
