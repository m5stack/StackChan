"""LED-frame stream subscriber that drives Stack-chan LEDs from WebSocket frames."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import json
import math
from typing import Any, Optional

import websockets

from .wifi_power_save import (
    acquire_wifi_power_save,
    reapply_wifi_power_save,
    release_wifi_power_save,
)

BASE_RING_LED_COUNT = 12
WS2812_MIN_LED_COUNT = 1
WS2812_MAX_LED_COUNT = 256
LED_STREAM_MAX_FPS = 30.0
WS2812_TARGET_TOOL_PREFIXES = {
    "port_b": "self.port_b.ws2812",
    "port_c": "self.port_c.ws2812",
}
LED_TARGETS = {"base_ring", *WS2812_TARGET_TOOL_PREFIXES}
LED_TARGET_ERROR = "target must be 'base_ring', 'port_b', or 'port_c'"
WS2812_COLOR_ORDERS = {"grb", "rgb"}
WS2812_COLOR_ORDER_ERROR = "color_order must be 'grb' or 'rgb'"


def _is_finite_number(value: Any) -> bool:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return False
    try:
        return math.isfinite(float(value))
    except (OverflowError, ValueError):
        return False


def _is_int_channel(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 255


@dataclass
class FollowLedStreamConfig:
    url: str
    target: str
    led_count: int | None = None
    max_fps: float = LED_STREAM_MAX_FPS
    color_order: str = "grb"
    source_filter: Optional[str] = None
    frame_filter: Optional[str] = None
    reconnect_initial_backoff_s: float = 1.5
    reconnect_max_backoff_s: float = 30.0

    def __post_init__(self) -> None:
        if not isinstance(self.url, str) or self.url.strip() == "":
            raise ValueError("url is required")
        if self.target not in LED_TARGETS:
            raise ValueError(LED_TARGET_ERROR)
        if self.color_order not in WS2812_COLOR_ORDERS:
            raise ValueError(WS2812_COLOR_ORDER_ERROR)
        if not _is_finite_number(self.max_fps) or not 0 < self.max_fps <= 30:
            raise ValueError("max_fps must be a number in (0, 30]")
        if self.target == "base_ring":
            if self.color_order != "grb":
                raise ValueError(
                    "color_order is only supported for target=port_b or target=port_c"
                )
            if self.led_count is not None and self.led_count != BASE_RING_LED_COUNT:
                raise ValueError("led_count for base_ring must be 12 when provided")
        else:
            if (
                not isinstance(self.led_count, int)
                or isinstance(self.led_count, bool)
                or not WS2812_MIN_LED_COUNT <= self.led_count <= WS2812_MAX_LED_COUNT
            ):
                raise ValueError(
                    "led_count is required for port_b/port_c and must be in 1..256"
                )
        if self.reconnect_initial_backoff_s <= 0:
            raise ValueError("reconnect_initial_backoff_s must be > 0")
        if self.reconnect_max_backoff_s <= 0:
            raise ValueError("reconnect_max_backoff_s must be > 0")

    @property
    def capacity(self) -> int:
        if self.target == "base_ring":
            return BASE_RING_LED_COUNT
        assert self.led_count is not None
        return self.led_count


class FollowLedStream:
    def __init__(self, gateway: Any, cfg: FollowLedStreamConfig) -> None:
        self._gateway = gateway
        self._cfg = cfg
        self._stop_event = asyncio.Event()
        self._task: asyncio.Task[None] | None = None
        self._connect_state = "init"
        self._frames_received = 0
        self._frames_sent = 0
        self._frames_dropped = 0
        self._last_frame_ts: int | None = None
        self._last_error: str | None = None
        self._last_sent_at: float | None = None
        self._target_ready = cfg.target == "base_ring"
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
            "target": self._cfg.target,
            "led_count": self._cfg.capacity,
            "max_fps": self._cfg.max_fps,
            "color_order": self._cfg.color_order,
            "source_filter": self._cfg.source_filter,
            "frame_filter": self._cfg.frame_filter,
            "connected": self._connect_state == "connected",
            "connect_state": self._connect_state,
            "frames_received": self._frames_received,
            "frames_sent": self._frames_sent,
            "frames_dropped": self._frames_dropped,
            "last_frame_ts": self._last_frame_ts,
            "last_error": self._last_error,
            "wifi_ps_apply_result": self._wifi_ps_apply_result,
            "wifi_ps_restore_result": self._wifi_ps_restore_result,
            "wifi_ps_previous": self._wifi_ps_previous,
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
        try:
            if self._is_ws2812_target():
                if not await self._ensure_target_ready():
                    raise RuntimeError(
                        self._last_error
                        or f"{self._cfg.target} WS2812 init failed"
                    )
            self._task = asyncio.create_task(
                self._run(),
                name="stackchan-follow-led-stream",
            )
        except Exception:
            self._wifi_ps_restore_result = await release_wifi_power_save(
                self._gateway.esp32
            )
            raise

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

    def _is_ws2812_target(self) -> bool:
        return self._cfg.target in WS2812_TARGET_TOOL_PREFIXES

    def _ws2812_tool_name(self, command: str) -> str:
        return f"{WS2812_TARGET_TOOL_PREFIXES[self._cfg.target]}.{command}"

    def _record_wifi_ps_previous(self, result: Any) -> None:
        if not isinstance(result, dict):
            return
        previous = result.get("previous")
        if isinstance(previous, str) and previous and previous != "unknown":
            self._wifi_ps_previous = previous

    def _invalidate_device_state(self) -> None:
        if self._is_ws2812_target():
            self._target_ready = False
        if isinstance(self._wifi_ps_apply_result, dict):
            self._wifi_ps_apply_result = {
                **self._wifi_ps_apply_result,
                "ok": False,
            }

    @staticmethod
    def _is_device_disconnect_error(error: Any) -> bool:
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
        if (
            isinstance(self._wifi_ps_apply_result, dict)
            and self._wifi_ps_apply_result.get("ok")
        ):
            return
        self._wifi_ps_apply_result = await reapply_wifi_power_save(
            self._gateway.esp32
        )
        self._record_wifi_ps_previous(self._wifi_ps_apply_result)

    async def _run(self) -> None:
        backoff = self._cfg.reconnect_initial_backoff_s
        try:
            while not self._stop_event.is_set():
                try:
                    self._connect_state = "connecting"
                    async with websockets.connect(self._cfg.url) as ws:
                        self._connect_state = "connected"
                        backoff = self._cfg.reconnect_initial_backoff_s
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

    async def _consume(self, ws: Any) -> None:
        async for msg in ws:
            if self._stop_event.is_set():
                break
            self._frames_received += 1
            frame = self._decode_frame(msg)
            if frame is None:
                self._frames_dropped += 1
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

            parsed = self._validate_frame(frame)
            if parsed is None:
                self._frames_dropped += 1
                continue
            ts, kind, colors = parsed
            self._last_frame_ts = int(ts)

            loop = asyncio.get_running_loop()
            now = loop.time()
            if (
                kind == "continuous"
                and self._last_sent_at is not None
                and now - self._last_sent_at < 1.0 / self._cfg.max_fps
            ):
                self._frames_dropped += 1
                continue

            await self._maybe_reapply_wifi_ps()
            if not await self._ensure_target_ready():
                self._frames_dropped += 1
                continue

            if not await self._dispatch_colors(colors):
                self._frames_dropped += 1
                continue

            self._last_sent_at = now
            self._frames_sent += 1

    @staticmethod
    def _decode_frame(msg: Any) -> dict[str, Any] | None:
        try:
            frame = json.loads(msg)
        except (TypeError, ValueError):
            return None
        if not isinstance(frame, dict):
            return None
        return frame

    def _validate_frame(
        self,
        frame: dict[str, Any],
    ) -> tuple[float, str, list[list[int]]] | None:
        ts = frame.get("ts")
        if not _is_finite_number(ts):
            return None
        kind = frame.get("kind")
        if kind not in {"event", "continuous"}:
            return None
        colors = self._validate_colors(frame.get("colors"))
        if colors is None:
            return None
        return float(ts), kind, colors

    def _validate_colors(self, value: Any) -> list[list[int]] | None:
        if not isinstance(value, list) or not value:
            return None
        if len(value) > self._cfg.capacity:
            return None
        colors: list[list[int]] = []
        for item in value:
            if not isinstance(item, list) or len(item) != 3:
                return None
            if not all(_is_int_channel(channel) for channel in item):
                return None
            colors.append([int(item[0]), int(item[1]), int(item[2])])
        return colors

    async def _ensure_target_ready(self) -> bool:
        if not self._is_ws2812_target() or self._target_ready:
            return True
        assert self._cfg.led_count is not None
        operation = f"{self._cfg.target} WS2812 init"
        from .stdio_server import _set_ws2812_color_order

        _set_ws2812_color_order(self._cfg.target, self._cfg.color_order)
        try:
            result, error = await self._gateway.esp32.call_tool(
                self._ws2812_tool_name("init"),
                {"led_count": self._cfg.led_count},
            )
        except Exception as exc:
            self._last_error = f"{operation} failed: {exc}"
            self._invalidate_device_state()
            return False
        if error:
            if isinstance(error, dict):
                self._last_error = str(error.get("message", error))
                if self._is_device_disconnect_error(error):
                    self._invalidate_device_state()
            else:
                self._last_error = str(error)
            return False

        failure = self._result_failure_reason(result, operation)
        if failure is not None:
            self._last_error = failure
            self._target_ready = False
            return False

        self._target_ready = True
        return True

    async def _dispatch_colors(
        self,
        colors: list[list[int]],
        *,
        retry_ws2812_reset: bool = True,
    ) -> bool:
        if self._cfg.target == "base_ring":
            tool_name = "self.led.set_many"
            colors_for_device = colors
        else:
            tool_name = self._ws2812_tool_name("set_strip")
            from .stdio_server import _remap_ws2812_colors_for_color_order

            colors_for_device = _remap_ws2812_colors_for_color_order(
                self._cfg.color_order,
                colors,
            )
        try:
            result, error = await self._gateway.esp32.call_tool(
                tool_name,
                {"colors": json.dumps(colors_for_device)},
            )
        except Exception as exc:
            self._last_error = str(exc)
            self._invalidate_device_state()
            return False

        if error:
            if isinstance(error, dict):
                self._last_error = str(error.get("message", error))
                if self._is_device_disconnect_error(error):
                    self._invalidate_device_state()
            else:
                self._last_error = str(error)
            return False

        failure = self._result_failure_reason(result, tool_name)
        if failure is not None:
            self._last_error = failure
            if self._is_ws2812_target():
                if self._result_is_unavailable(result):
                    self._invalidate_device_state()
                    if retry_ws2812_reset:
                        await self._maybe_reapply_wifi_ps()
                        if not await self._ensure_target_ready():
                            return False
                        return await self._dispatch_colors(
                            colors,
                            retry_ws2812_reset=False,
                        )
                    return False
                self._target_ready = False
            return False
        return True

    @classmethod
    def _result_is_unavailable(cls, result: Any) -> bool:
        if not isinstance(result, dict):
            return False
        payload = cls._decode_call_result_payload(result)
        if payload is None:
            payload = result
        return isinstance(payload, dict) and payload.get("available") is False

    @classmethod
    def _result_failure_reason(cls, result: Any, operation: str) -> str | None:
        if not isinstance(result, dict):
            return None
        if result.get("isError"):
            return f"{operation} returned isError"
        payload = cls._decode_call_result_payload(result)
        if payload is None:
            payload = result
        if not isinstance(payload, dict):
            return None
        if payload.get("isError"):
            return f"{operation} payload returned isError"
        if payload.get("available") is False:
            error = payload.get("error")
            if isinstance(error, str) and error:
                return f"{operation} unavailable: {error}"
            return f"{operation} unavailable"
        if payload.get("ok") is False:
            error = payload.get("error")
            if isinstance(error, str) and error:
                return f"{operation} failed: {error}"
            return f"{operation} reported ok=false"
        return None

    @staticmethod
    def _decode_call_result_payload(result: dict[str, Any]) -> Optional[dict[str, Any]]:
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


_follower: Optional[FollowLedStream] = None
_follower_lock: Optional[asyncio.Lock] = None


def _get_follower_lock() -> asyncio.Lock:
    global _follower_lock
    if _follower_lock is None:
        _follower_lock = asyncio.Lock()
    return _follower_lock


async def start_follow(gateway: Any, cfg: FollowLedStreamConfig) -> dict[str, Any]:
    """Cancel previous follower if running, then start a new one."""
    global _follower
    async with _get_follower_lock():
        if _follower is not None:
            await _follower.stop()
        follower = FollowLedStream(gateway, cfg)
        _follower = follower
        try:
            await follower.start()
        except Exception:
            _follower = None
            raise
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
