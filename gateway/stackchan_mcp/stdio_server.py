"""stdio MCP server for MCP client.

Exposes stackchan tools via the MCP Python SDK's stdio transport.
Each tool call is relayed to the connected ESP32 device.
"""

from __future__ import annotations

from contextlib import AsyncExitStack
import inspect
import json
import logging
import os
from datetime import datetime
from typing import Any, Literal, cast

import anyio
from mcp.server import InitializationOptions, NotificationOptions, Server
from mcp.server.session import ServerSession
from mcp.server.stdio import stdio_server
from mcp.types import ImageContent, Notification, TextContent, Tool

from . import __version__
from .agent_status import AgentSnapshot, AgentTask
from .gateway import get_gateway
from .notify_config import NotifyConfig, load_notify_config
from .user_defaults import resolve_default
from .stt import listen_and_transcribe
from .tts import synthesize_and_send

logger = logging.getLogger(__name__)

STACKCHAN_EVENT_METHOD = "stackchan/event"
CHANNEL_NOTIFICATION_METHOD = "notifications/claude/channel"
CHANNEL_CAPABILITY = "claude/channel"
_SUPPORTED_EVENT_METHODS = {STACKCHAN_EVENT_METHOD, CHANNEL_NOTIFICATION_METHOD}
FOLLOW_LED_TARGETS = {"base_ring", "port_b", "port_c"}
FOLLOW_LED_WS2812_TARGETS = {"port_b", "port_c"}
FOLLOW_LED_TARGET_ERROR = "target must be 'base_ring', 'port_b', or 'port_c'"
WS2812_COLOR_ORDERS = {"grb", "rgb"}
WS2812_COLOR_ORDER_ERROR = "color_order must be 'grb' or 'rgb'"
# Process-local init state. Re-running a Port B/C WS2812 init call updates the
# selected order for that port; no persistence is needed across gateway restarts.
_WS2812_COLOR_ORDER_BY_PORT = {
    "port_b": "grb",
    "port_c": "grb",
}
STACKCHAN_EVENT_INSTRUCTIONS = (
    "Stack-chan physical events arrive as server-initiated "
    "notifications with method='stackchan/event'. Params include "
    "event_type ('touch'), subtype ('tap' or 'stroke'), "
    "duration_ms, ts, session_id. When such a notification "
    "arrives, react naturally using existing tools "
    "(set_avatar, say, set_mouth, set_leds, move_head). There is "
    "no dedicated reply tool — the existing tool palette is the "
    "reaction surface."
)
STACKCHAN_CHANNEL_INSTRUCTIONS = (
    'Stack-chan physical events arrive as Channels notifications under '
    '<channel source="plugin:stackchanmcp:stackchanmcp" action="..." '
    'subtype="..." duration_ms="...">. React naturally using existing '
    'tools (set_avatar, say, set_mouth, set_leds, move_head).'
)
STACKCHAN_JSONL_INSTRUCTIONS = (
    "Stack-chan physical events are persisted to the JSONL log; host "
    "integration consumes them externally."
)

PRESET_DPS = {
    "low": 30,
    "mid": 120,
    "high": 240,
}
SPEED_DPS_MAX = 10000
SPEED_DESCRIPTION = """speed (optional): How fast to move the head.
  - "low"  — slow, deliberate, ~30°/s. Good for curious tilts or gentle look-toward.
  - "mid"  — default natural turn, ~120°/s. Use for conversational eye contact.
  - "high" — quick reaction, ~240°/s. Use for surprise / double-take.
  - Or a raw degrees-per-second integer if you need a specific value."""

_active_session: Any | None = None
_active_sessions: dict[int, Any] = {}


def _reset_ws2812_color_orders_for_tests() -> None:
    _WS2812_COLOR_ORDER_BY_PORT.update({"port_b": "grb", "port_c": "grb"})


def _set_ws2812_color_order(port: str, color_order: str) -> None:
    if port not in _WS2812_COLOR_ORDER_BY_PORT:
        raise ValueError(f"unsupported WS2812 port: {port}")
    if color_order not in WS2812_COLOR_ORDERS:
        raise ValueError(WS2812_COLOR_ORDER_ERROR)
    _WS2812_COLOR_ORDER_BY_PORT[port] = color_order


def _get_ws2812_color_order(port: str) -> str:
    return _WS2812_COLOR_ORDER_BY_PORT.get(port, "grb")


def _remap_ws2812_pixel_args_for_color_order(
    color_order: str,
    arguments: dict[str, Any],
) -> dict[str, Any]:
    if color_order != "rgb":
        return arguments
    if "r" not in arguments or "g" not in arguments:
        return arguments
    return {
        **arguments,
        "r": arguments["g"],
        "g": arguments["r"],
    }


def _remap_ws2812_pixel_args_for_device(
    port: str,
    arguments: dict[str, Any],
) -> dict[str, Any]:
    return _remap_ws2812_pixel_args_for_color_order(
        _get_ws2812_color_order(port),
        arguments,
    )


def _remap_ws2812_colors_for_color_order(
    color_order: str,
    colors: Any,
) -> Any:
    if color_order != "rgb":
        return colors
    if not isinstance(colors, list):
        return colors
    remapped: list[Any] = []
    for color in colors:
        if isinstance(color, list) and len(color) == 3:
            remapped.append([color[1], color[0], color[2]])
        else:
            remapped.append(color)
    return remapped


def _remap_ws2812_colors_for_device(
    port: str,
    colors: Any,
) -> Any:
    return _remap_ws2812_colors_for_color_order(
        _get_ws2812_color_order(port),
        colors,
    )


class StackChanEventNotification(
    Notification[dict[str, Any], Literal["stackchan/event"]]
):
    method: Literal["stackchan/event"] = "stackchan/event"
    params: dict[str, Any]


class StackChanChannelNotification(
    Notification[dict[str, Any], Literal["notifications/claude/channel"]]
):
    method: Literal["notifications/claude/channel"] = "notifications/claude/channel"
    params: dict[str, Any]


class StackChanServer(Server):
    def __init__(self, name: str, *, notify_config: NotifyConfig) -> None:
        super().__init__(name)
        self._notify_config = notify_config

    def create_initialization_options(
        self,
        notification_options: NotificationOptions | None = None,
        experimental_capabilities: dict[str, dict[str, Any]] | None = None,
    ) -> InitializationOptions:
        if notification_options is None and experimental_capabilities is None:
            return _create_initialization_options(self, self._notify_config)
        return super().create_initialization_options(
            notification_options=notification_options,
            experimental_capabilities=experimental_capabilities,
        )

    async def run(
        self,
        read_stream: Any,
        write_stream: Any,
        initialization_options: InitializationOptions,
        raise_exceptions: bool = False,
        stateless: bool = False,
    ) -> None:
        global _active_session
        session: Any | None = None
        try:
            async with AsyncExitStack() as stack:
                lifespan_context = await stack.enter_async_context(self.lifespan(self))
                session = await stack.enter_async_context(
                    ServerSession(
                        read_stream,
                        write_stream,
                        initialization_options,
                        stateless=stateless,
                    )
                )
                _active_session = session
                _active_sessions[id(session)] = session

                task_support = (
                    self._experimental_handlers.task_support
                    if self._experimental_handlers
                    else None
                )
                if task_support is not None:
                    task_support.configure_session(session)
                    await stack.enter_async_context(task_support.run())

                async with anyio.create_task_group() as tg:
                    try:
                        async for message in session.incoming_messages:
                            logger.debug("Received message: %s", message)
                            tg.start_soon(
                                self._handle_message,
                                message,
                                session,
                                lifespan_context,
                                raise_exceptions,
                            )
                    finally:
                        tg.cancel_scope.cancel()
        finally:
            if session is not None:
                _active_sessions.pop(id(session), None)
            _active_session = _latest_active_session()

    async def _handle_message(
        self,
        message: Any,
        session: Any,
        lifespan_context: Any,
        raise_exceptions: bool = False,
    ) -> None:
        global _active_session
        _active_session = session
        _active_sessions[id(session)] = session
        await super()._handle_message(
            message,
            session,
            lifespan_context,
            raise_exceptions,
    )


def _latest_active_session() -> Any | None:
    if not _active_sessions:
        return None
    return next(reversed(_active_sessions.values()))


async def notify_stackchan_event(method: str, params: dict[str, Any]) -> None:
    """Forward a stackchan event to the connected MCP client."""
    if method not in _SUPPORTED_EVENT_METHODS:
        logger.warning("Unsupported stackchan event notification method: %s", method)
        return

    sessions = list(_active_sessions.values())
    if not sessions and _active_session is not None:
        sessions = [_active_session]
    if not sessions:
        logger.warning("Cannot emit %s notification: no active MCP session", method)
        return

    notification = _build_stackchan_notification(method, params)
    for session in sessions:
        try:
            await session.send_notification(cast(Any, notification))
        except Exception as exc:  # pragma: no cover - depends on client transport failure
            logger.warning("Failed to emit %s notification: %s", method, exc)


def _build_stackchan_notification(
    method: str,
    params: dict[str, Any],
) -> StackChanEventNotification | StackChanChannelNotification:
    if method == STACKCHAN_EVENT_METHOD:
        return StackChanEventNotification(params=params)
    return StackChanChannelNotification(params=params)


def _build_experimental_capabilities(
    notify_config: NotifyConfig,
) -> dict[str, dict[str, Any]]:
    capabilities: dict[str, dict[str, Any]] = {}
    if notify_config.legacy_event_enabled:
        capabilities[STACKCHAN_EVENT_METHOD] = {}
    if notify_config.channels_enabled:
        capabilities[CHANNEL_CAPABILITY] = {}
    return capabilities


def _build_stackchan_event_instructions(notify_config: NotifyConfig) -> str | None:
    fragments = []
    if notify_config.channels_enabled:
        fragments.append(STACKCHAN_CHANNEL_INSTRUCTIONS)
    if notify_config.legacy_event_enabled:
        fragments.append(STACKCHAN_EVENT_INSTRUCTIONS)
    if (
        notify_config.jsonl_enabled
        and not notify_config.channels_enabled
        and not notify_config.legacy_event_enabled
    ):
        fragments.append(STACKCHAN_JSONL_INSTRUCTIONS)
    if not fragments:
        return None
    return "\n\n".join(fragments)


def _create_initialization_options(
    server: Server,
    notify_config: NotifyConfig,
) -> InitializationOptions:
    return InitializationOptions(
        server_name="stackchanmcp",
        server_version=__version__,
        capabilities=server.get_capabilities(
            notification_options=NotificationOptions(),
            experimental_capabilities=_build_experimental_capabilities(notify_config),
        ),
        instructions=_build_stackchan_event_instructions(notify_config),
    )


def _verify_mcp_sdk_compatibility() -> None:
    """Fail fast if the installed MCP SDK no longer exposes the private
    attributes that ``StackChanServer`` depends on.

    ``StackChanServer`` mirrors a slimmed-down copy of ``Server.run()`` so it
    can capture the active ``ServerSession`` for server-initiated
    ``stackchan/event`` notifications. The public MCP SDK currently does not
    offer a stable hook for this, so the subclass touches
    ``Server._experimental_handlers`` and ``Server._handle_message`` directly.

    These private members are pinned by the ``mcp>=1.27,<2.0`` range declared
    in ``pyproject.toml``. This guard adds an extra safety net so the gateway
    fails with a clear ``RuntimeError`` at startup rather than silently
    dropping notifications or crashing mid-message if a future installation
    somehow resolves a wholly incompatible SDK shape.
    """

    probe = Server("compat-check")

    if not hasattr(probe, "_experimental_handlers"):
        raise RuntimeError(
            "stackchan-mcp gateway requires `mcp.server.Server._experimental_handlers` "
            "to exist on instances. The installed MCP SDK appears to have removed or "
            "renamed this attribute; pin `mcp` to a verified 1.x release."
        )

    handle = getattr(probe, "_handle_message", None)
    if not callable(handle) or not inspect.iscoroutinefunction(handle):
        raise RuntimeError(
            "stackchan-mcp gateway requires `mcp.server.Server._handle_message` to be "
            "an async callable. The installed MCP SDK does not expose it in the "
            "expected shape; pin `mcp` to a verified 1.x release."
        )

    try:
        sig = inspect.signature(handle)
    except (TypeError, ValueError) as exc:
        raise RuntimeError(
            "stackchan-mcp gateway could not introspect "
            "`mcp.server.Server._handle_message` signature on the installed MCP SDK; "
            "pin `mcp` to a verified 1.x release."
        ) from exc

    positional = [
        p
        for p in sig.parameters.values()
        if p.kind
        in (
            inspect.Parameter.POSITIONAL_OR_KEYWORD,
            inspect.Parameter.POSITIONAL_ONLY,
            inspect.Parameter.VAR_POSITIONAL,
        )
    ]
    if len(positional) < 4:
        raise RuntimeError(
            "stackchan-mcp gateway requires `mcp.server.Server._handle_message` to "
            "accept at least 4 positional arguments "
            "(message, session, lifespan_context, raise_exceptions); the installed "
            f"MCP SDK exposes {sig}. Pin `mcp` to a verified 1.x release."
        )


def _resolve_speed_dps(speed: Any) -> int | None:
    """Return an int speed_dps to forward, or None to omit the field."""
    if speed is None:
        return None
    if isinstance(speed, bool):
        raise TypeError("speed must be a preset string or an integer, not bool")
    if isinstance(speed, str):
        if speed not in PRESET_DPS:
            raise ValueError(
                f"speed preset must be one of {list(PRESET_DPS)}, got {speed!r}"
            )
        return PRESET_DPS[speed]
    if isinstance(speed, int):
        if speed < 1:
            raise ValueError(f"speed integer must be >= 1, got {speed}")
        if speed > SPEED_DPS_MAX:
            raise ValueError(f"speed integer must be <= {SPEED_DPS_MAX}, got {speed}")
        return speed
    raise TypeError(
        f"speed must be 'low' / 'mid' / 'high' / int / None, got {type(speed).__name__}"
    )


def _follow_pose_text(payload: dict[str, Any]) -> list[TextContent]:
    return [TextContent(type="text", text=json.dumps(payload))]


def _follow_pose_error(message: str) -> list[TextContent]:
    return _follow_pose_text({"ok": False, "error": message})


def _follow_led_error(message: str) -> list[TextContent]:
    return _follow_pose_text({"ok": False, "error": message})


def _beat_error(message: str) -> list[TextContent]:
    return _follow_pose_text({"ok": False, "error": message})


def _is_int_arg(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_number_arg(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _parse_rgb_color(value: Any) -> tuple[int, int, int] | None:
    if value is None:
        return None
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError("color must be an RGB array: [r, g, b]")
    channels: list[int] = []
    for channel in value:
        if not _is_int_arg(channel) or not 0 <= channel <= 255:
            raise ValueError("color channels must be integers in 0..255")
        channels.append(int(channel))
    return channels[0], channels[1], channels[2]


def _optional_non_empty_string(
    arguments: dict[str, Any],
    name: str,
) -> tuple[str | None, str | None]:
    value = arguments.get(name)
    if value is None:
        return None, None
    if not isinstance(value, str) or value == "":
        return None, f"{name} must be a non-empty string or null"
    return value, None


def _resolve_optional_non_empty_string(
    arguments: dict[str, Any],
    tool_name: str,
    name: str,
) -> tuple[str | None, str | None]:
    value = (
        arguments[name]
        if name in arguments
        else resolve_default(tool_name, name, None)
    )
    if value is None:
        return None, None
    if not isinstance(value, str) or value == "":
        return None, f"{name} must be a non-empty string or null"
    return value, None


async def _handle_follow_pose_stream(
    gateway: Any,
    arguments: dict[str, Any],
) -> list[TextContent]:
    from .follow_pose_stream import (
        FollowPoseStreamConfig,
        get_follow_status,
        start_follow,
        stop_follow,
    )

    action = arguments.get("action", "start")
    if action not in {"start", "stop", "status"}:
        return _follow_pose_error("action must be one of: start, stop, status")

    if action == "status":
        return _follow_pose_text({"ok": True, **get_follow_status()})

    if action == "stop":
        status = await stop_follow()
        return _follow_pose_text({"ok": True, **status})

    url_value = arguments.get("url")
    if not isinstance(url_value, str) or url_value.strip() == "":
        return _follow_pose_error("url is required when action=start")
    url = url_value.strip()
    if not (url.startswith("ws://") or url.startswith("wss://")):
        return _follow_pose_error("url must start with ws:// or wss://")

    tool_name = "stackchan_follow_pose_stream"

    flip_yaw = (
        arguments["flip_yaw"]
        if "flip_yaw" in arguments
        else resolve_default(tool_name, "flip_yaw", 1)
    )
    if not _is_int_arg(flip_yaw) or flip_yaw not in (-1, 1):
        return _follow_pose_error("flip_yaw must be -1 or 1")

    flip_pitch = (
        arguments["flip_pitch"]
        if "flip_pitch" in arguments
        else resolve_default(tool_name, "flip_pitch", 1)
    )
    if not _is_int_arg(flip_pitch) or flip_pitch not in (-1, 1):
        return _follow_pose_error("flip_pitch must be -1 or 1")

    pitch_center_deg = (
        arguments["pitch_center_deg"]
        if "pitch_center_deg" in arguments
        else resolve_default(tool_name, "pitch_center_deg", 45)
    )
    if (
        not _is_int_arg(pitch_center_deg)
        or not 5 <= pitch_center_deg <= 85
    ):
        return _follow_pose_error("pitch_center_deg must be an integer in 5..85")

    smoothing_window = (
        arguments["smoothing_window"]
        if "smoothing_window" in arguments
        else resolve_default(tool_name, "smoothing_window", 5)
    )
    if not _is_int_arg(smoothing_window) or not 1 <= smoothing_window <= 20:
        return _follow_pose_error("smoothing_window must be an integer in 1..20")

    downsample_hz = (
        arguments["downsample_hz"]
        if "downsample_hz" in arguments
        else resolve_default(tool_name, "downsample_hz", 20.0)
    )
    if not _is_number_arg(downsample_hz) or not 0 < downsample_hz <= 20:
        return _follow_pose_error("downsample_hz must be a number in (0, 20]")

    max_step_deg = (
        arguments["max_step_deg"]
        if "max_step_deg" in arguments
        else resolve_default(tool_name, "max_step_deg", 12.0)
    )
    if not _is_number_arg(max_step_deg) or not 0 < max_step_deg <= 30:
        return _follow_pose_error("max_step_deg must be a number in (0, 30]")

    speed_dps = (
        arguments["speed_dps"]
        if "speed_dps" in arguments
        else resolve_default(tool_name, "speed_dps", 240)
    )
    if not _is_int_arg(speed_dps) or not 1 <= speed_dps <= 240:
        return _follow_pose_error("speed_dps must be an integer in 1..240")

    source_filter, error = _optional_non_empty_string(arguments, "source_filter")
    if error:
        return _follow_pose_error(error)
    frame_filter, error = _optional_non_empty_string(arguments, "frame_filter")
    if error:
        return _follow_pose_error(error)

    cfg = FollowPoseStreamConfig(
        url=url,
        source_filter=source_filter,
        frame_filter=frame_filter,
        flip_yaw=flip_yaw,
        flip_pitch=flip_pitch,
        pitch_center_deg=pitch_center_deg,
        smoothing_window=smoothing_window,
        downsample_hz=float(downsample_hz),
        max_step_deg=float(max_step_deg),
        speed_dps=speed_dps,
    )
    status = await start_follow(gateway, cfg)
    return _follow_pose_text({"ok": True, **status})


async def _handle_follow_led_stream(
    gateway: Any,
    arguments: dict[str, Any],
) -> list[TextContent]:
    from .follow_led_stream import (
        FollowLedStreamConfig,
        get_follow_status,
        start_follow,
        stop_follow,
    )

    action = arguments.get("action", "start")
    if action not in {"start", "stop", "status"}:
        return _follow_led_error("action must be one of: start, stop, status")

    if action == "status":
        return _follow_pose_text({"ok": True, **get_follow_status()})

    if action == "stop":
        status = await stop_follow()
        return _follow_pose_text({"ok": True, **status})

    url_value = arguments.get("url")
    if not isinstance(url_value, str) or url_value.strip() == "":
        return _follow_led_error("url is required when action=start")
    url = url_value.strip()
    if not (url.startswith("ws://") or url.startswith("wss://")):
        return _follow_led_error("url must start with ws:// or wss://")

    tool_name = "stackchan_follow_led_stream"

    target = (
        arguments["target"]
        if "target" in arguments
        else resolve_default(tool_name, "target", None)
    )
    if target not in FOLLOW_LED_TARGETS:
        return _follow_led_error(FOLLOW_LED_TARGET_ERROR)

    led_count = (
        arguments["led_count"]
        if "led_count" in arguments
        else resolve_default(tool_name, "led_count", None)
    )
    if target in FOLLOW_LED_WS2812_TARGETS:
        if not _is_int_arg(led_count) or not 1 <= led_count <= 256:
            return _follow_led_error(
                "led_count is required for port_b/port_c and must be an "
                "integer in 1..256"
            )
    elif led_count is not None:
        if not _is_int_arg(led_count) or led_count != 12:
            return _follow_led_error(
                "led_count for base_ring must be 12 when provided"
            )

    color_order = (
        arguments["color_order"]
        if "color_order" in arguments
        else resolve_default(tool_name, "color_order", "grb")
    )
    if color_order not in WS2812_COLOR_ORDERS:
        return _follow_led_error(WS2812_COLOR_ORDER_ERROR)
    if target == "base_ring" and color_order != "grb":
        return _follow_led_error(
            "color_order is only supported for target=port_b or target=port_c"
        )

    max_fps = (
        arguments["max_fps"]
        if "max_fps" in arguments
        else resolve_default(tool_name, "max_fps", 30.0)
    )
    if not _is_number_arg(max_fps) or not 0 < float(max_fps) <= 30:
        return _follow_led_error("max_fps must be a number in (0, 30]")

    reconnect_initial_backoff_s = (
        arguments["reconnect_initial_backoff_s"]
        if "reconnect_initial_backoff_s" in arguments
        else resolve_default(tool_name, "reconnect_initial_backoff_s", 1.5)
    )
    if (
        not _is_number_arg(reconnect_initial_backoff_s)
        or float(reconnect_initial_backoff_s) <= 0
    ):
        return _follow_led_error("reconnect_initial_backoff_s must be > 0")

    reconnect_max_backoff_s = (
        arguments["reconnect_max_backoff_s"]
        if "reconnect_max_backoff_s" in arguments
        else resolve_default(tool_name, "reconnect_max_backoff_s", 30.0)
    )
    if (
        not _is_number_arg(reconnect_max_backoff_s)
        or float(reconnect_max_backoff_s) <= 0
    ):
        return _follow_led_error("reconnect_max_backoff_s must be > 0")

    source_filter, error = _resolve_optional_non_empty_string(
        arguments,
        tool_name,
        "source_filter",
    )
    if error:
        return _follow_led_error(error)
    frame_filter, error = _resolve_optional_non_empty_string(
        arguments,
        tool_name,
        "frame_filter",
    )
    if error:
        return _follow_led_error(error)

    try:
        cfg = FollowLedStreamConfig(
            url=url,
            target=target,
            led_count=led_count,
            max_fps=float(max_fps),
            color_order=color_order,
            source_filter=source_filter,
            frame_filter=frame_filter,
            reconnect_initial_backoff_s=float(reconnect_initial_backoff_s),
            reconnect_max_backoff_s=float(reconnect_max_backoff_s),
        )
        status = await start_follow(gateway, cfg)
    except (ValueError, RuntimeError) as exc:
        return _follow_led_error(str(exc))
    return _follow_pose_text({"ok": True, **status})


async def _handle_beat_mode_start(
    gateway: Any,
    arguments: dict[str, Any],
) -> list[TextContent]:
    from .beat import BeatModeConfig, start_beat_mode

    motion_intensity = arguments.get("motion_intensity", 0.5)
    if (
        not _is_number_arg(motion_intensity)
        or not 0.0 <= float(motion_intensity) <= 1.0
    ):
        return _beat_error("motion_intensity must be a number in 0..1")

    sensitivity = arguments.get("sensitivity", 0.5)
    if (
        not _is_number_arg(sensitivity)
        or not 0.0 <= float(sensitivity) <= 1.0
    ):
        return _beat_error("sensitivity must be a number in 0..1")

    try:
        color = _parse_rgb_color(arguments.get("color")) or (0, 160, 255)
    except ValueError as exc:
        return _beat_error(str(exc))

    duration_sec = arguments.get("duration_sec")
    if duration_sec is not None and (
        not _is_int_arg(duration_sec) or duration_sec <= 0
    ):
        return _beat_error("duration_sec must be a positive integer or null")

    try:
        cfg = BeatModeConfig(
            motion_intensity=float(motion_intensity),
            sensitivity=float(sensitivity),
            color=color,
            duration_sec=duration_sec,
        )
        status = await start_beat_mode(gateway, cfg)
    except (ValueError, RuntimeError) as exc:
        return _beat_error(str(exc))
    return _follow_pose_text({"ok": True, **status})


async def _handle_beat_mode_stop() -> list[TextContent]:
    from .beat import stop_beat_mode

    status = await stop_beat_mode()
    return _follow_pose_text({"ok": True, **status})


async def _handle_beat_mode_update(arguments: dict[str, Any]) -> list[TextContent]:
    from .beat import update_beat_mode

    updates: dict[str, Any] = {}
    if "motion_intensity" in arguments:
        value = arguments["motion_intensity"]
        if not _is_number_arg(value) or not 0.0 <= float(value) <= 1.0:
            return _beat_error("motion_intensity must be a number in 0..1")
        updates["motion_intensity"] = float(value)

    if "sensitivity" in arguments:
        value = arguments["sensitivity"]
        if not _is_number_arg(value) or not 0.0 <= float(value) <= 1.0:
            return _beat_error("sensitivity must be a number in 0..1")
        updates["sensitivity"] = float(value)

    if "color" in arguments:
        try:
            color = _parse_rgb_color(arguments["color"])
        except ValueError as exc:
            return _beat_error(str(exc))
        if color is None:
            return _beat_error("color must be an RGB array: [r, g, b]")
        updates["color"] = color

    if "blink_rate" in arguments:
        value = arguments["blink_rate"]
        if not _is_number_arg(value) or not 0.25 <= float(value) <= 4.0:
            return _beat_error("blink_rate must be a number in 0.25..4")
        updates["blink_rate"] = float(value)

    for name in ("motion_enabled", "led_enabled"):
        if name in arguments:
            value = arguments[name]
            if not isinstance(value, bool):
                return _beat_error(f"{name} must be a boolean")
            updates[name] = value

    try:
        status = await update_beat_mode(**updates)
    except (ValueError, RuntimeError) as exc:
        return _beat_error(str(exc))
    return _follow_pose_text({"ok": True, **status})


def _handle_beat_meta_snapshot() -> list[TextContent]:
    from .beat import get_beat_mode_snapshot

    return _follow_pose_text({"ok": True, **get_beat_mode_snapshot()})


async def _handle_beat_clip_save(arguments: dict[str, Any]) -> list[TextContent]:
    from .beat import save_beat_clip

    seconds = arguments.get("seconds", 10.0)
    if not _is_number_arg(seconds) or float(seconds) <= 0:
        return _beat_error("seconds must be a positive number")
    try:
        result = await save_beat_clip(float(seconds))
    except (ValueError, RuntimeError) as exc:
        return _beat_error(str(exc))
    return _follow_pose_text({"ok": True, **result})


async def _dispatch_mcp_tool(
    name: str,
    arguments: dict[str, Any],
    gateway: Any,
) -> list[TextContent | ImageContent]:
    """Run one StackChan MCP tool against the provided gateway instance."""
    # Optional multi-device routing key (declared by _with_device_id_schema);
    # strip so it never reaches ESP32.
    device_id = arguments.pop("device_id", None)
    if device_id is not None and not isinstance(device_id, str):
        return [
            TextContent(
                type="text",
                text=json.dumps(
                    {"error": f"device_id must be a string (got {device_id!r})"}
                ),
            )
        ]
    if isinstance(device_id, str) and not device_id:
        device_id = None

    if name == "agent_update_snapshot":
        try:
            raw_tasks = arguments["tasks"]
            if not isinstance(raw_tasks, list):
                raise ValueError("tasks must be a list")
            parsed_tasks = tuple(
                AgentTask(
                    id=task["id"], text=task["text"], status=task["status"]
                )
                for task in raw_tasks
                if isinstance(task, dict)
            )
            if len(parsed_tasks) != len(raw_tasks):
                raise ValueError("each task must be an object")
            snapshot = AgentSnapshot(
                agent=arguments["agent"],
                session_id=arguments["session_id"],
                sequence=arguments["sequence"],
                observed_at=datetime.fromisoformat(arguments["observed_at"]),
                status=arguments["status"],
                stage=arguments["stage"],
                tasks=parsed_tasks,
            )
            result = await gateway.update_agent_snapshot(snapshot)
            return [
                TextContent(
                    type="text",
                    text=json.dumps(
                        {
                            "accepted": result.accepted,
                            "sequence": result.current.sequence if result.current else None,
                        }
                    ),
                )
            ]
        except (KeyError, TypeError, ValueError) as exc:
            return [TextContent(type="text", text=json.dumps({"error": str(exc)}))]

    if name == "agent_get_dashboard":
        dashboard = await gateway.get_agent_dashboard()
        return [TextContent(type="text", text=json.dumps(dashboard, ensure_ascii=False))]

    if name == "get_status":
        status = gateway.esp32.get_status()
        return [TextContent(type="text", text=json.dumps(status, indent=2))]

    if name == "list_devices":
        devices = gateway.esp32.list_devices()
        return [
            TextContent(
                type="text",
                text=json.dumps(
                    {
                        "devices": devices,
                        "default_device_id": gateway.esp32.default_device_id,
                    },
                    indent=2,
                ),
            )
        ]

    if name == "set_default_device":
        target = arguments.get("device_id", device_id)
        if not isinstance(target, str) or not target:
            return [
                TextContent(
                    type="text",
                    text=json.dumps(
                        {"ok": False, "error": "device_id is required"}
                    ),
                )
            ]
        result = gateway.esp32.set_default_device(target)
        return [TextContent(type="text", text=json.dumps(result))]

    if name == "say":
        try:
            result = await synthesize_and_send(
                arguments, gateway=gateway, device_id=device_id
            )
        except (ValueError, NotImplementedError, RuntimeError) as exc:
            return [
                TextContent(
                    type="text",
                    text=json.dumps({"error": str(exc)}),
                )
            ]
        return [TextContent(type="text", text=json.dumps(result))]

    if name == "listen":
        try:
            result = await listen_and_transcribe(
                arguments, gateway=gateway, device_id=device_id
            )
        except (ValueError, NotImplementedError, RuntimeError) as exc:
            return [
                TextContent(
                    type="text",
                    text=json.dumps({"error": str(exc)}),
                )
            ]
        return [TextContent(type="text", text=json.dumps(result))]

    if name == "load_avatar_set":
        archive_path = arguments.get("archive_path", "")
        mode = arguments.get("mode", "")
        try:
            timeout = float(arguments.get("timeout", 60.0))
        except (TypeError, ValueError):
            timeout = 60.0
        if not archive_path or not isinstance(archive_path, str):
            return [
                TextContent(
                    type="text",
                    text=json.dumps(
                        {"ok": False, "error": "archive_path is required"}
                    ),
                )
            ]
        if mode not in ("layered", "matrix"):
            return [
                TextContent(
                    type="text",
                    text=json.dumps({"ok": False, "error": f"unknown mode: {mode}"}),
                )
            ]
        result = await gateway.load_avatar_set(
            archive_path, mode, timeout, device_id=device_id
        )
        return [TextContent(type="text", text=json.dumps(result))]

    if name == "upgrade_firmware":
        firmware_path = arguments.get("firmware_path", "")
        if not firmware_path or not isinstance(firmware_path, str):
            return [
                TextContent(
                    type="text",
                    text=json.dumps(
                        {"ok": False, "error": "firmware_path is required"}
                    ),
                )
            ]
        try:
            timeout = float(arguments.get("timeout", 180.0))
        except (TypeError, ValueError):
            timeout = 180.0
        result = await gateway.upgrade_firmware(
            firmware_path, timeout, device_id=device_id
        )
        return [TextContent(type="text", text=json.dumps(result))]

    if name == "stackchan_follow_pose_stream":
        return await _handle_follow_pose_stream(gateway, arguments)

    if name == "stackchan_follow_led_stream":
        return await _handle_follow_led_stream(gateway, arguments)

    if name == "beat_mode_start":
        return await _handle_beat_mode_start(gateway, arguments)

    if name == "beat_mode_stop":
        return await _handle_beat_mode_stop()

    if name == "beat_mode_update":
        return await _handle_beat_mode_update(arguments)

    if name == "beat_meta_snapshot":
        return _handle_beat_meta_snapshot()

    if name == "beat_clip_save":
        return await _handle_beat_clip_save(arguments)

    if not gateway.esp32.device_connected:
        return [
            TextContent(
                type="text",
                text=json.dumps(
                    {"error": "No ESP32 device connected. Please check the device."}
                ),
            )
        ]

    if name == "move_head":
        yaw_val = arguments.get("yaw")
        pitch_val = arguments.get("pitch")
        if (
            not isinstance(yaw_val, int)
            or isinstance(yaw_val, bool)
            or not (-90 <= yaw_val <= 90)
        ):
            return [
                TextContent(
                    type="text",
                    text=json.dumps(
                        {
                            "error": (
                                "yaw must be an integer in -90..90 "
                                f"(got {yaw_val!r})"
                            )
                        }
                    ),
                )
            ]
        if (
            not isinstance(pitch_val, int)
            or isinstance(pitch_val, bool)
            or not (5 <= pitch_val <= 85)
        ):
            return [
                TextContent(
                    type="text",
                    text=json.dumps(
                        {
                            "error": (
                                "pitch must be an integer in 5..85 "
                                "(M5Stack-recommended operating range; "
                                "for the wider firmware hard clamp "
                                "0..88 use `set_head_angles`). got "
                                f"{pitch_val!r}"
                            )
                        }
                    ),
                )
            ]
        try:
            speed_dps = _resolve_speed_dps(arguments.get("speed"))
        except (TypeError, ValueError) as exc:
            return [
                TextContent(
                    type="text",
                    text=json.dumps({"error": str(exc)}),
                )
            ]
        arguments = {"yaw": yaw_val, "pitch": pitch_val}
        if speed_dps is not None:
            arguments["speed_dps"] = speed_dps

    ws2812_port_by_init_tool = {
        "port_b_ws2812_init": "port_b",
        "port_c_ws2812_init": "port_c",
    }
    ws2812_port_by_set_pixel_tool = {
        "port_b_ws2812_set_pixel": "port_b",
        "port_c_ws2812_set_pixel": "port_c",
    }
    ws2812_port_by_set_strip_tool = {
        "port_b_ws2812_set_strip": "port_b",
        "port_c_ws2812_set_strip": "port_c",
    }
    if name in ws2812_port_by_init_tool:
        color_order = arguments.get("color_order", "grb")
        if color_order not in WS2812_COLOR_ORDERS:
            return [
                TextContent(
                    type="text",
                    text=json.dumps({"error": WS2812_COLOR_ORDER_ERROR}),
                )
            ]
        _set_ws2812_color_order(ws2812_port_by_init_tool[name], color_order)
        arguments = {"led_count": arguments.get("led_count")}
    elif name in ws2812_port_by_set_pixel_tool:
        arguments = _remap_ws2812_pixel_args_for_device(
            ws2812_port_by_set_pixel_tool[name],
            arguments,
        )
    elif name in ws2812_port_by_set_strip_tool:
        port = ws2812_port_by_set_strip_tool[name]
        arguments = {
            **arguments,
            "colors": _remap_ws2812_colors_for_device(
                port,
                arguments.get("colors", []),
            ),
        }

    tool_map: dict[str, tuple[str, dict[str, Any]]] = {
        "get_device_info": (
            "self.get_device_status",
            {},
        ),
        "take_photo": (
            "self.camera.take_photo",
            arguments,
        ),
        "set_volume": (
            "self.audio_speaker.set_volume",
            arguments,
        ),
        "set_brightness": (
            "self.screen.set_brightness",
            arguments,
        ),
        "set_off_timeout": (
            "self.screen.set_off_timeout",
            arguments,
        ),
        "get_off_timeout": (
            "self.screen.get_off_timeout",
            {},
        ),
        "set_external_power_auto_off": (
            "self.power.set_external_power_auto_off",
            arguments,
        ),
        "get_external_power_auto_off": (
            "self.power.get_external_power_auto_off",
            {},
        ),
        "set_charge_protection": (
            "self.power.set_charge_protection",
            arguments,
        ),
        "get_charge_protection": (
            "self.power.get_charge_protection",
            {},
        ),
        "set_charge_thresholds": (
            "self.power.set_charge_thresholds",
            arguments,
        ),
        "move_head": (
            "self.robot.set_head_angles",
            arguments,
        ),
        "get_head_angles": (
            "self.robot.get_head_angles",
            {},
        ),
        "gpio_test": (
            "self.robot.gpio_test",
            {},
        ),
        "uart_diag": (
            "self.robot.uart_diag",
            {},
        ),
        "check_vm_en": (
            "self.robot.check_vm_en",
            {},
        ),
        "gateway_config_get": (
            "self.gateway_config.get",
            {},
        ),
        "gateway_config_set": (
            "self.gateway_config.set",
            arguments,
        ),
        "get_touch_sensor_enabled": (
            "self.robot.get_touch_sensor_enabled",
            {},
        ),
        "set_touch_sensor_enabled": (
            "self.robot.set_touch_sensor_enabled",
            arguments,
        ),
        "set_avatar": (
            "self.display.set_avatar",
            arguments,
        ),
        "set_mouth": (
            "self.display.set_mouth",
            arguments,
        ),
        "set_mouth_sequence": (
            "self.display.set_mouth_sequence",
            {"steps_json": json.dumps(arguments.get("steps", []))},
        ),
        "set_blink": (
            "self.display.set_blink",
            arguments,
        ),
        "set_servo_torque": (
            "self.robot.set_servo_torque",
            arguments,
        ),
        "set_auto_torque_release": (
            "self.robot.set_auto_torque_release",
            arguments,
        ),
        "get_touch_state": (
            "self.touch.get_touch_state",
            {},
        ),
        "set_led": (
            "self.led.set_color",
            arguments,
        ),
        "set_all_leds": (
            "self.led.set_all",
            arguments,
        ),
        "set_leds": (
            "self.led.set_many",
            {"colors": json.dumps(arguments.get("colors", []))},
        ),
        "clear_leds": (
            "self.led.clear",
            {},
        ),
        "port_b_ws2812_init": (
            "self.port_b.ws2812.init",
            arguments,
        ),
        "port_b_ws2812_set_pixel": (
            "self.port_b.ws2812.set_pixel",
            arguments,
        ),
        "port_b_ws2812_set_strip": (
            "self.port_b.ws2812.set_strip",
            {"colors": json.dumps(arguments.get("colors", []))},
        ),
        "port_b_ws2812_refresh": (
            "self.port_b.ws2812.refresh",
            {},
        ),
        "port_b_ws2812_clear": (
            "self.port_b.ws2812.clear",
            {},
        ),
        "port_c_ws2812_init": (
            "self.port_c.ws2812.init",
            arguments,
        ),
        "port_c_ws2812_set_pixel": (
            "self.port_c.ws2812.set_pixel",
            arguments,
        ),
        "port_c_ws2812_set_strip": (
            "self.port_c.ws2812.set_strip",
            {"colors": json.dumps(arguments.get("colors", []))},
        ),
        "port_c_ws2812_refresh": (
            "self.port_c.ws2812.refresh",
            {},
        ),
        "port_c_ws2812_clear": (
            "self.port_c.ws2812.clear",
            {},
        ),
        "i2c_scan": (
            "self.i2c.scan",
            {},
        ),
        "i2c_read": (
            "self.i2c.read",
            arguments,
        ),
        "i2c_write": (
            "self.i2c.write",
            arguments,
        ),
        "i2c_write_read": (
            "self.i2c.write_read",
            arguments,
        ),
    }

    if name not in tool_map:
        return [
            TextContent(
                type="text",
                text=json.dumps({"error": f"Unknown tool: {name}"}),
            )
        ]

    esp32_name, esp32_args = tool_map[name]
    # device_id was stripped above; route to the selected (or default) device.
    result, error = await gateway.esp32.call_tool(
        esp32_name, esp32_args, device_id=device_id
    )

    if error:
        return [
            TextContent(
                type="text",
                text=json.dumps({"error": error.get("message", str(error))}),
            )
        ]

    if isinstance(result, dict):
        content = result.get("content", [])
        if content and isinstance(content, list):
            texts = []
            for item in content:
                if isinstance(item, dict) and item.get("type") == "text":
                    texts.append(item.get("text", ""))
            if texts:
                joined = "\n".join(texts)
                if name == "take_photo":
                    return _photo_contents(joined)
                return [TextContent(type="text", text=joined)]

        return [TextContent(type="text", text=json.dumps(result, indent=2))]

    return [TextContent(type="text", text=str(result))]


#: Refuse to inline images beyond this size — tool receipts should stay
#: small for LLM clients (base64 of 200 KB ≈ 270 KB of text). ESP32
#: camera JPEGs are a few KB, so this cap only guards against
#: pathological captures.
_PHOTO_INLINE_MAX_BYTES = 200 * 1024


def _photo_contents(result_text: str) -> list[TextContent | ImageContent]:
    """take_photo receipt → text + inline ImageContent.

    The capture JSON carries ``image_path``. Returning the JPEG as an MCP
    image block alongside the text lets LLM clients actually see the
    frame instead of only reading a file path they may not be able to
    reach. Any failure to inline degrades to the original text-only
    receipt.

    ``image_path`` originates in a device response, so only files inside
    the capture directory are eligible — the gateway must not inline an
    arbitrary readable local file on a malformed or hostile device's say-so.
    """
    import base64

    from .capture_server import CAPTURE_DIR

    out: list[TextContent | ImageContent] = [
        TextContent(type="text", text=result_text)
    ]
    try:
        payload = json.loads(result_text)
        image_path = payload.get("image_path") if isinstance(payload, dict) else None
        if isinstance(image_path, str) and image_path:
            resolved = os.path.realpath(image_path)
            capture_root = os.path.realpath(CAPTURE_DIR)
            if not resolved.startswith(capture_root + os.sep):
                logger.warning(
                    "take_photo inline image skipped: %s is outside the "
                    "capture directory",
                    image_path,
                )
                return out
            with open(resolved, "rb") as f:
                raw = f.read()
            if raw[:2] == b"\xff\xd8" and len(raw) <= _PHOTO_INLINE_MAX_BYTES:
                out.append(
                    ImageContent(
                        type="image",
                        data=base64.b64encode(raw).decode(),
                        mimeType="image/jpeg",
                    )
                )
            else:
                logger.info(
                    "take_photo inline image skipped: %s is %s",
                    image_path,
                    "not a JPEG"
                    if raw[:2] != b"\xff\xd8"
                    else f"{len(raw)} bytes (cap {_PHOTO_INLINE_MAX_BYTES})",
                )
    except (OSError, TypeError, ValueError) as exc:
        logger.warning("take_photo inline image skipped: %s", exc)
    return out


# Tools that never take a per-call device_id: gateway-local status/registry
# tools and the streaming/beat helpers, which always act on the default device.
DEVICE_ID_EXEMPT_TOOLS = frozenset(
    {
        "get_status",
        "agent_update_snapshot",
        "agent_get_dashboard",
        "list_devices",
        "set_default_device",
        "stackchan_follow_pose_stream",
        "stackchan_follow_led_stream",
        "beat_mode_start",
        "beat_mode_stop",
        "beat_mode_update",
        "beat_meta_snapshot",
        "beat_clip_save",
    }
)

_DEVICE_ID_SCHEMA: dict[str, Any] = {
    "type": "string",
    "description": (
        "Optional Device-Id of the connected ESP32 to target (see "
        "list_devices). Omit to use the default device."
    ),
}


def _with_device_id_schema(tools: list[Tool]) -> list[Tool]:
    """Declare the optional ``device_id`` routing argument on routed tools."""
    for tool in tools:
        if tool.name in DEVICE_ID_EXEMPT_TOOLS:
            continue
        schema = dict(tool.inputSchema)
        properties = dict(schema.get("properties") or {})
        properties.setdefault("device_id", dict(_DEVICE_ID_SCHEMA))
        schema["properties"] = properties
        tool.inputSchema = schema
    return tools


def create_server(notify_config: NotifyConfig | None = None) -> StackChanServer:
    """Create and configure the MCP server with tool handlers."""
    _verify_mcp_sdk_compatibility()
    if notify_config is None:
        notify_config = load_notify_config()
    server = StackChanServer("stackchanmcp", notify_config=notify_config)

    @server.list_tools()
    async def list_tools() -> list[Tool]:
        """List available stackchan tools.

        Tools prefixed with ESP32 names (self.*) are relayed to the device.
        get_status is handled locally by the gateway.
        """
        tools = [
            Tool(
                name="agent_update_snapshot",
                description=(
                    "Submit the latest privacy-limited Codex or Claude status and task list. "
                    "Only stage labels and short task text are accepted; do not send prompts, "
                    "file paths, tool arguments, or conversation content."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "agent": {"type": "string", "enum": ["codex", "claude"]},
                        "session_id": {"type": "string", "maxLength": 128},
                        "sequence": {"type": "integer", "minimum": 0},
                        "observed_at": {"type": "string", "format": "date-time"},
                        "status": {"type": "string", "enum": ["idle", "working", "waiting", "completed", "failed"]},
                        "stage": {"type": "string", "maxLength": 24},
                        "tasks": {
                            "type": "array",
                            "maxItems": 12,
                            "items": {
                                "type": "object",
                                "properties": {
                                    "id": {"type": "string", "maxLength": 128},
                                    "text": {"type": "string", "maxLength": 64},
                                    "status": {"type": "string", "enum": ["pending", "in_progress", "completed"]},
                                },
                                "required": ["id", "text", "status"],
                            },
                        },
                    },
                    "required": ["agent", "session_id", "sequence", "observed_at", "status", "stage", "tasks"],
                },
            ),
            Tool(
                name="agent_get_dashboard",
                description="Get the latest Codex and Claude dashboard snapshots in fixed left/right order.",
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="get_status",
                description=(
                    "Get the gateway's connection status: whether ESP32 is connected, "
                    "device info, and list of available device tools."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {},
                },
            ),
            Tool(
                name="list_devices",
                description=(
                    "List ESP32 devices currently connected to the gateway. "
                    "Returns each device_id (from the Device-Id header), "
                    "connection time, and which one is the default routing target. "
                    "Use set_default_device to change the default; pass optional "
                    "device_id on other tools to target a specific unit."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {},
                },
            ),
            Tool(
                name="set_default_device",
                description=(
                    "Set the default ESP32 destination used when tools omit "
                    "device_id. Until set, the default is the first device that "
                    "connected. Returns an error if the device_id is not connected."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "device_id": {
                            "type": "string",
                            "description": (
                                "Device-Id of a currently connected ESP32 "
                                "(see list_devices)."
                            ),
                        },
                    },
                    "required": ["device_id"],
                },
            ),
            Tool(
                name="get_device_info",
                description=(
                    "Get real-time device information from ESP32: "
                    "battery level, speaker volume, screen brightness, network status, etc."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {},
                },
            ),
            Tool(
                name="take_photo",
                description=(
                    "Take a photo with the robot's camera and ask a question about it. "
                    "The device captures an image and returns an AI-generated description."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "question": {
                            "type": "string",
                            "description": "Question to ask about the photo (e.g. 'What do you see?')",
                        },
                    },
                    "required": ["question"],
                },
            ),
            Tool(
                name="set_volume",
                description="Set the speaker volume (0-100).",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "volume": {
                            "type": "integer",
                            "description": "Volume level (0-100)",
                        },
                    },
                    "required": ["volume"],
                },
            ),
            Tool(
                name="set_brightness",
                description="Set the screen brightness (0-100).",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "brightness": {
                            "type": "integer",
                            "description": "Brightness level (0-100)",
                        },
                    },
                    "required": ["brightness"],
                },
            ),
            Tool(
                name="set_off_timeout",
                description=(
                    "Set the idle screen-off timeout in seconds (0-86400). "
                    "Default: 300; 0 disables screen-off. Saved in NVS and "
                    "preserved across reboots. Requires firmware 1.17.0 or later."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "seconds": {
                            "type": "integer",
                            "minimum": 0,
                            "maximum": 86400,
                        },
                    },
                    "required": ["seconds"],
                },
            ),
            Tool(
                name="get_off_timeout",
                description=(
                    "Get the persistent idle screen-off timeout in seconds. "
                    "0 means disabled; default: 300. "
                    "Requires firmware 1.17.0 or later."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="set_external_power_auto_off",
                description=(
                    "Persist and immediately apply whether the device also "
                    "dims (60 s idle) and powers off (300 s idle) while on "
                    "external power. Off by default: on external power it "
                    "never does this automatically; on battery it always does. "
                    "Requires firmware-v1.18.0 or later."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "enabled": {
                            "type": "boolean",
                            "description": (
                                "True to dim and power off on idle even on "
                                "external power; false to stay on while "
                                "powered."
                            ),
                        },
                    },
                    "required": ["enabled"],
                },
            ),
            Tool(
                name="get_external_power_auto_off",
                description=(
                    "Get the persistent external_power_auto_off setting, "
                    "whether the battery is discharging, and whether the "
                    "idle dim/power-off timer is running. "
                    "Requires firmware-v1.18.0 or later."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="set_charge_protection",
                description=(
                    "Persist and immediately apply battery charge protection. "
                    "Protection is opt-in by default and uses the persisted "
                    "ON/OFF thresholds. Disable it temporarily before going out to "
                    "allow the AXP2101 charger to reach its own full-charge "
                    "termination; enable it again to resume protection."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "enabled": {
                            "type": "boolean",
                            "description": (
                                "True for persistent threshold protection; "
                                "false to allow charging to the PMIC's "
                                "full-charge termination."
                            ),
                        },
                    },
                    "required": ["enabled"],
                },
            ),
            Tool(
                name="get_charge_protection",
                description=(
                    "Get the persistent and effective charge_protection "
                    "setting, thresholds, and current charger-enable state. "
                    "The setting is opt-in when it has never been saved."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {},
                },
            ),
            Tool(
                name="set_charge_thresholds",
                description=(
                    "Persist and immediately apply charge-protection thresholds. "
                    "on_below must be lower than off_above."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "on_below": {
                            "type": "integer",
                            "minimum": 0,
                            "maximum": 100,
                            "description": "Enable charging at or below this percentage.",
                        },
                        "off_above": {
                            "type": "integer",
                            "minimum": 0,
                            "maximum": 100,
                            "description": "Disable charging at or above this percentage.",
                        },
                    },
                    "required": ["on_below", "off_above"],
                },
            ),
            Tool(
                name="move_head",
                description=(
                    "Move the robot's head to safe, recommended angles. "
                    "yaw: horizontal (-90 to 90), pitch: vertical (5 to 85, "
                    "the M5Stack-recommended operating range). Out-of-range "
                    "requests are rejected at this MCP layer; for advanced "
                    "callers that need the firmware hard clamp (pitch 0..88), "
                    "use the firmware-side `set_head_angles` device tool, "
                    "which exposes a permissive schema and the authoritative "
                    "two-tier guard described in the README."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "yaw": {
                            "type": "integer",
                            "description": "Horizontal angle in degrees (-90 to 90)",
                            "minimum": -90,
                            "maximum": 90,
                        },
                        "pitch": {
                            "type": "integer",
                            "description": (
                                "Vertical angle in degrees (5 to 85, "
                                "M5Stack-recommended operating range). For the "
                                "wider firmware hard clamp (0..88), use the "
                                "`set_head_angles` device tool instead."
                            ),
                            "minimum": 5,
                            "maximum": 85,
                        },
                        "speed": {
                            "oneOf": [
                                {"enum": ["low", "mid", "high"]},
                                {
                                    "type": "integer",
                                    "minimum": 1,
                                    "maximum": SPEED_DPS_MAX,
                                },
                            ],
                            "description": SPEED_DESCRIPTION,
                        },
                    },
                    "required": ["yaw", "pitch"],
                },
            ),
            Tool(
                name="stackchan_follow_pose_stream",
                description=(
                    "Subscribes to an arbitrary upstream WebSocket pose-stream "
                    "using action=start, stop, or status. Sensor yaw is "
                    "forwarded 1:1 and clamped to +/-90 degrees; sensor "
                    "pitch is shifted by pitch_center_deg (default 45) so "
                    "sensor neutral maps to head neutral, then clamped to "
                    "5..85 degrees. Inputs beyond the head's mechanical range "
                    "saturate at the limit without scaling. The subscriber "
                    "applies moving-average smoothing, a downsample cap, and "
                    "an angular-velocity clamp. Only one subscription is "
                    "active at a time; a new start cancels the previous task. "
                    "Connections reconnect with exponential backoff and are "
                    "stopped cleanly when the gateway shuts down."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "action": {
                            "type": "string",
                            "enum": ["start", "stop", "status"],
                            "default": "start",
                            "description": "Lifecycle control. Default is 'start'.",
                        },
                        "url": {
                            "type": "string",
                            "description": (
                                "WebSocket URL (ws:// or wss://) to subscribe to. "
                                "Required when action=start."
                            ),
                        },
                        "source_filter": {
                            "type": "string",
                            "description": (
                                "Optional: ignore frames whose top-level 'source' "
                                "field does not equal this string."
                            ),
                        },
                        "frame_filter": {
                            "type": "string",
                            "description": (
                                "Optional: ignore frames whose top-level 'frame' "
                                "field does not equal this string "
                                "(e.g. 'calibrated')."
                            ),
                        },
                        "flip_yaw": {
                            "type": "integer",
                            "enum": [-1, 1],
                            "default": 1,
                            "description": (
                                "Multiplier applied to sensor yaw before clamping. "
                                "Use -1 if the upstream IMU yaw convention is "
                                "reversed."
                            ),
                        },
                        "flip_pitch": {
                            "type": "integer",
                            "enum": [-1, 1],
                            "default": 1,
                        },
                        "pitch_center_deg": {
                            "type": "integer",
                            "default": 45,
                            "minimum": 5,
                            "maximum": 85,
                            "description": (
                                "Servo pitch (deg) treated as the sensor-pitch=0 "
                                "anchor. Defaults to the head's neutral pose."
                            ),
                        },
                        "smoothing_window": {
                            "type": "integer",
                            "default": 5,
                            "minimum": 1,
                            "maximum": 20,
                            "description": (
                                "Moving-average window size for incoming sensor "
                                "frames. 1 = passthrough (disable gateway-side "
                                "smoothing). Default 5."
                            ),
                        },
                        "downsample_hz": {
                            "type": "number",
                            "default": 20,
                            "exclusiveMinimum": 0,
                            "maximum": 20,
                            "description": (
                                "Cap servo command rate. Recent frames are "
                                "smoothed; commands are issued at most this "
                                "frequently. Capped at 20 to match the "
                                "SCS0009 servo's observed sustained WritePos "
                                "rate; higher continuous rates can trigger "
                                "UART hang."
                            ),
                        },
                        "max_step_deg": {
                            "type": "number",
                            "default": 12,
                            "exclusiveMinimum": 0,
                            "maximum": 30,
                            "description": (
                                "Per-tick angular delta limit. With "
                                "downsample_hz=20 and max_step_deg=12 the "
                                "effective angular velocity is bounded by "
                                "240 dps."
                            ),
                        },
                        "speed_dps": {
                            "type": "integer",
                            "default": 240,
                            "minimum": 1,
                            "maximum": 240,
                            "description": (
                                "speed_dps forwarded to set_head_angles per "
                                "command. Capped at the SCS0009 datasheet "
                                "working speed (240)."
                            ),
                        },
                    },
                },
            ),
            Tool(
                name="stackchan_follow_led_stream",
                description=(
                    "Subscribes to an arbitrary upstream WebSocket LED-frame "
                    "stream using action=start, stop, or status, then forwards "
                    "validated color frames to either the 12-LED base ring or "
                    "a Port B or Port C WS2812 strip. Frames contain ts, kind, "
                    "and colors; "
                    "kind='continuous' is capped by max_fps while kind='event' "
                    "bypasses the rate gate for beat flashes. Only one LED "
                    "subscription is active at a time; a new start cancels the "
                    "previous task. Connections reconnect with exponential "
                    "backoff and are stopped cleanly when the gateway shuts down."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "action": {
                            "type": "string",
                            "enum": ["start", "stop", "status"],
                            "default": "start",
                            "description": "Lifecycle control. Default is 'start'.",
                        },
                        "url": {
                            "type": "string",
                            "description": (
                                "WebSocket URL (ws:// or wss://) to subscribe to. "
                                "Required when action=start."
                            ),
                        },
                        "target": {
                            "type": "string",
                            "enum": ["base_ring", "port_b", "port_c"],
                            "description": (
                                "LED target. base_ring uses the built-in 12 LEDs; "
                                "port_b uses a WS2812 strip on Port B; port_c "
                                "uses a WS2812 strip on Port C."
                            ),
                        },
                        "led_count": {
                            "type": "integer",
                            "minimum": 1,
                            "maximum": 256,
                            "description": (
                                "Required for target=port_b or target=port_c. "
                                "For base_ring, omit or pass 12."
                            ),
                        },
                        "color_order": {
                            "type": "string",
                            "enum": ["grb", "rgb"],
                            "default": "grb",
                            "description": (
                                "WS2812 strip color order for target=port_b or "
                                "target=port_c. Use rgb for RGB-wired LEDs; the "
                                "gateway swaps R/G before forwarding to the "
                                "firmware. base_ring only supports grb."
                            ),
                        },
                        "max_fps": {
                            "type": "number",
                            "default": 30,
                            "exclusiveMinimum": 0,
                            "maximum": 30,
                            "description": (
                                "Maximum rate for kind='continuous' frames. "
                                "kind='event' frames bypass this gate. "
                                "Practical guidance from on-device measurement: "
                                "wire round-trip is ~40-50 ms per frame on a "
                                "typical home WLAN, so ~20 fps is the effective "
                                "ceiling — 20 is the recommended value for live "
                                "use. Excess continuous frames are dropped; "
                                "event frames are never dropped."
                            ),
                        },
                        "source_filter": {
                            "type": "string",
                            "description": (
                                "Optional: ignore frames whose top-level 'source' "
                                "field does not equal this string."
                            ),
                        },
                        "frame_filter": {
                            "type": "string",
                            "description": (
                                "Optional: ignore frames whose top-level 'frame' "
                                "field does not equal this string."
                            ),
                        },
                        "reconnect_initial_backoff_s": {
                            "type": "number",
                            "default": 1.5,
                            "exclusiveMinimum": 0,
                            "description": "Initial reconnect backoff in seconds.",
                        },
                        "reconnect_max_backoff_s": {
                            "type": "number",
                            "default": 30,
                            "exclusiveMinimum": 0,
                            "description": "Maximum reconnect backoff in seconds.",
                        },
                    },
                },
            ),
            Tool(
                name="get_head_angles",
                description="Get the robot's current head angles: yaw and pitch in degrees.",
                inputSchema={
                    "type": "object",
                    "properties": {},
                },
            ),
            Tool(
                name="gpio_test",
                description="Test GPIO6 pin by toggling HIGH/LOW 5 times. Check if servo reacts.",
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="uart_diag",
                description="Send raw servo bytes via UART and report write result.",
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="check_vm_en",
                description=(
                    "Diagnostic: read PY32 REG_GPIO_O_L and report whether VM EN "
                    "(pin 0 = servo power) is currently HIGH. Returns "
                    "{io_expander_present, i2c_read_ok, raw, vm_en_high}."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="gateway_config_get",
                description=(
                    "Read the device's NVS-backed WebSocket gateway "
                    "connection settings. Returns url, fallback_url, "
                    "token_set (never the token value), forced_keys, "
                    "force_mode, discovery_enabled, and connected_url when "
                    "the current WebSocket candidate is connected. Empty url "
                    "enables mDNS discovery when firmware discovery support "
                    "is compiled in and the primary URL is not forced; "
                    "fallback_url is tried after discovery and is suitable "
                    "for an out-of-LAN relay. forced_keys lists any of url, "
                    "fallback_url, and token that a non-empty Kconfig default "
                    "overrides at connect time; force_mode=true means at "
                    "least one key is forced."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="gateway_config_set",
                description=(
                    "Update the device's NVS-backed WebSocket gateway "
                    "connection settings. Optional string fields: url, "
                    "fallback_url, token; at least one must be provided. "
                    "Passing an empty string clears that key. Leave url "
                    "empty to enable mDNS discovery on the next reconnect "
                    "when firmware discovery support is compiled in and the "
                    "primary URL is not forced; fallback_url is tried after "
                    "discovery and is suitable for an out-of-LAN relay. The "
                    "change is persisted but does not disconnect, reconnect, "
                    "or reboot the device; it takes effect on the next "
                    "reconnect. forced_keys lists any of url, fallback_url, "
                    "and token that a non-empty Kconfig default overrides at "
                    "connect time; force_mode=true means at least one key is "
                    "forced, so updates to those keys are ignored until a "
                    "non-force build is flashed."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "url": {
                            "type": "string",
                            "description": (
                                "Primary NVS WebSocket URL. Empty string "
                                "clears websocket.url, which enables mDNS "
                                "discovery on the next reconnect when "
                                "discovery is compiled in and the primary URL "
                                "is not forced."
                            ),
                        },
                        "fallback_url": {
                            "type": "string",
                            "description": (
                                "Fallback NVS WebSocket URL. Tried after "
                                "discovery and suitable for an out-of-LAN "
                                "relay. Empty string clears "
                                "websocket.fallback_url."
                            ),
                        },
                        "token": {
                            "type": "string",
                            "description": (
                                "Bearer token stored in NVS and sent to the "
                                "gateway. Empty string clears the token. The "
                                "token value is never returned by "
                                "gateway_config_get."
                            ),
                        },
                    },
                },
            ),
            Tool(
                name="get_touch_sensor_enabled",
                description=(
                    "Read the device's NVS-backed head-touch sensor enable "
                    "flag. When disabled, the firmware stops both the local "
                    "motion response and the MCP stackchan/event emission; "
                    "the setting persists across reboot."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="set_touch_sensor_enabled",
                description=(
                    "Enable or disable the device's head-touch sensor at "
                    "runtime. The NVS-backed setting persists across reboot. "
                    "Disabling stops both the firmware local motion response "
                    "and the MCP stackchan/event emission."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "enabled": {
                            "type": "boolean",
                            "description": (
                                "True to enable tap/stroke detection; false "
                                "to disable local reactions and event emission."
                            ),
                        },
                    },
                    "required": ["enabled"],
                },
            ),
            Tool(
                name="set_avatar",
                description=(
                    "Switch the avatar face shown on the LCD. "
                    "Choose one of the supported faces; this is the robot's "
                    "actual visible expression, not just a label. "
                    "Pass 'off' to hide the avatar and disable blink, exposing the "
                    "underlying xiaozhi-esp32 screens (WiFi config UI, OTA, settings); "
                    "any other face brings the avatar back and restores blink."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "face": {
                            "type": "string",
                            "enum": [
                                "idle",
                                "happy",
                                "thinking",
                                "sad",
                                "surprised",
                                "embarrassed",
                                "off",
                            ],
                            "description": (
                                "One of: idle, happy, thinking, sad, surprised, "
                                "embarrassed, off."
                            ),
                        },
                    },
                    "required": ["face"],
                },
            ),
            Tool(
                name="set_mouth",
                description=(
                    "Set the avatar mouth shape for lip-sync. "
                    "The shape is held until the next set_avatar / set_mouth call, "
                    "or until an autonomous blink restores the resting face. "
                    "Calling this while a set_mouth_sequence is in flight "
                    "interrupts the sequence."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "mouth": {
                            "type": "string",
                            "enum": ["closed", "half", "open", "e", "u"],
                            "description": "One of: closed, half, open, e, u.",
                        },
                    },
                    "required": ["mouth"],
                },
            ),
            Tool(
                name="set_mouth_sequence",
                description=(
                    "Queue a lip-sync sequence and play it on the device. "
                    "Each step holds 'shape' for 'duration_ms' before "
                    "advancing. The firmware walks the queue locally so "
                    "there is no per-step network RTT (use this instead of "
                    "issuing many set_mouth calls back-to-back from a TTS "
                    "loop). Returns immediately with the queued step count "
                    "and estimated total duration. Calling set_mouth, "
                    "set_avatar, or this tool again interrupts the in-flight "
                    "sequence and replaces it. Autonomous blink is paused "
                    "while a sequence is playing and resumed when it ends. "
                    "The final shape is held until the next "
                    "set_mouth / set_avatar call, or until an autonomous "
                    "blink restores the resting face — this is the same "
                    "Phase 2 trade-off that applies to set_mouth, since the "
                    "blink animation ends by repainting the full face. If "
                    "the final shape must persist visually, disable blink "
                    "with set_blink(false) before the sequence (or append a "
                    "closed step if you just want the mouth to close at "
                    "the end)."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "steps": {
                            "type": "array",
                            "minItems": 1,
                            "maxItems": 256,
                            "items": {
                                "type": "object",
                                "properties": {
                                    "shape": {
                                        "type": "string",
                                        "enum": ["closed", "half", "open", "e", "u"],
                                        "description": (
                                            "Mouth shape for this step. "
                                            "One of: closed, half, open, e, u."
                                        ),
                                    },
                                    "duration_ms": {
                                        "type": "integer",
                                        "minimum": 10,
                                        "maximum": 10000,
                                        "description": (
                                            "How long to hold this shape "
                                            "before advancing, in ms (10..10000)."
                                        ),
                                    },
                                },
                                "required": ["shape", "duration_ms"],
                            },
                            "description": (
                                "Ordered list of mouth shapes with hold "
                                "durations (1..256 steps)."
                            ),
                        },
                    },
                    "required": ["steps"],
                },
            ),
            Tool(
                name="set_blink",
                description=(
                    "Enable or disable autonomous eye blinking. "
                    "When enabled, the avatar blinks every 3-6 seconds at random."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "enabled": {
                            "type": "boolean",
                            "description": "True to start blinking, false to stop.",
                        },
                    },
                    "required": ["enabled"],
                },
            ),
            Tool(
                name="set_servo_torque",
                description=(
                    "Enable or disable SCS0009 servo torque on the yaw / "
                    "pitch axes independently. Disabling torque stops motor "
                    "current on that axis; the head holds via static "
                    "friction (no motion is commanded). On disable, the "
                    "firmware also cancels any in-flight MotionDriver "
                    "interpolation and marks the axis position unknown so "
                    "a subsequent same-target set_head_angles is re-"
                    "dispatched rather than no-op-optimized. Re-enabling "
                    "torque does NOT trigger a move; the next "
                    "set_head_angles or wobble call will. Diagnostic / "
                    "power-management primitive used to observe physical "
                    "head behavior under torque-off (Issue #163; auto "
                    "release on idle is Issue #152 Phase 4)."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "yaw_enabled": {
                            "type": "boolean",
                            "description": (
                                "True to enable yaw axis torque, false to "
                                "disable."
                            ),
                        },
                        "pitch_enabled": {
                            "type": "boolean",
                            "description": (
                                "True to enable pitch axis torque, false "
                                "to disable."
                            ),
                        },
                    },
                    "required": ["yaw_enabled", "pitch_enabled"],
                },
            ),
            Tool(
                name="set_auto_torque_release",
                description=(
                    "Enable or disable firmware-side automatic SCS0009 "
                    "torque release after motion idle timeout. timeout_ms "
                    "is clamped by the firmware to 500..600000 ms. "
                    "Disabling this setting does not re-enable torque if "
                    "it is already released; the next set_head_angles, "
                    "wobble, or explicit set_servo_torque(true, true) call "
                    "re-engages torque."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "enabled": {
                            "type": "boolean",
                            "description": (
                                "True to enable idle auto-release, false "
                                "to disable it."
                            ),
                        },
                        "timeout_ms": {
                            "type": "integer",
                            "description": (
                                "Idle timeout in milliseconds. Values "
                                "outside 500..600000 are clamped by the "
                                "firmware handler."
                            ),
                        },
                    },
                    "required": ["enabled", "timeout_ms"],
                },
            ),
            Tool(
                name="get_touch_state",
                description=(
                    "Read the head-touch (Si12T) sensor state and the most recent "
                    "gesture event (tap/stroke/idle). Returns per-zone booleans, "
                    "the raw output byte, and how long ago the last event fired."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {},
                },
            ),
            Tool(
                name="set_led",
                description=(
                    "Set a single RGB LED on the StackChan base. There are 12 LEDs "
                    "arranged in two rows of 6 (index 0..11). Updates immediately."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "index": {
                            "type": "integer",
                            "description": "LED index (0..11)",
                            "minimum": 0,
                            "maximum": 11,
                        },
                        "r": {"type": "integer", "description": "Red 0..255", "minimum": 0, "maximum": 255},
                        "g": {"type": "integer", "description": "Green 0..255", "minimum": 0, "maximum": 255},
                        "b": {"type": "integer", "description": "Blue 0..255", "minimum": 0, "maximum": 255},
                    },
                    "required": ["index", "r", "g", "b"],
                },
            ),
            Tool(
                name="set_all_leds",
                description="Set all 12 RGB LEDs on the StackChan base to the same color. Updates immediately.",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "r": {"type": "integer", "description": "Red 0..255", "minimum": 0, "maximum": 255},
                        "g": {"type": "integer", "description": "Green 0..255", "minimum": 0, "maximum": 255},
                        "b": {"type": "integer", "description": "Blue 0..255", "minimum": 0, "maximum": 255},
                    },
                    "required": ["r", "g", "b"],
                },
            ),
            Tool(
                name="set_leds",
                description=(
                    "Set multiple RGB LEDs in one shot. 'colors' is an array of "
                    "[r,g,b] triples starting at index 0 (e.g. [[255,0,0],[0,255,0]]). "
                    "Up to 12 entries; extras are ignored, missing entries keep their "
                    "previous color. Use this for animations / patterns to avoid 12x "
                    "I2C round-trips."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "colors": {
                            "type": "array",
                            "description": "Array of [r,g,b] triples, each 0..255",
                            "items": {
                                "type": "array",
                                "items": {"type": "integer", "minimum": 0, "maximum": 255},
                                "minItems": 3,
                                "maxItems": 3,
                            },
                            "minItems": 1,
                            "maxItems": 12,
                        },
                    },
                    "required": ["colors"],
                },
            ),
            Tool(
                name="clear_leds",
                description="Turn off all 12 RGB LEDs on the StackChan base.",
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="port_b_ws2812_init",
                description=(
                    "Initialize a WS2812-compatible LED strip connected to "
                    "Port B (CoreS3 HY2.0-4P digital OUTPUT, GPIO 9). "
                    "led_count is the number of LEDs in the strip (1..256). "
                    "Repeated calls with the same led_count are no-ops; a "
                    "different led_count rebuilds the strip handle. Port B "
                    "outputs 3.3 V CMOS data on GPIO 9; older strict 5 V "
                    "WS2812 variants may require a level shifter."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "led_count": {
                            "type": "integer",
                            "description": "Number of LEDs in the strip (1..256).",
                            "minimum": 1,
                            "maximum": 256,
                        },
                        "color_order": {
                            "type": "string",
                            "enum": ["grb", "rgb"],
                            "default": "grb",
                            "description": (
                                "Logical LED color order. Use grb for standard "
                                "WS2812/NeoPixel strips, or rgb for RGB-wired "
                                "LEDs; the gateway swaps R/G before forwarding "
                                "colors to the firmware."
                            ),
                        },
                    },
                    "required": ["led_count"],
                },
            ),
            Tool(
                name="port_b_ws2812_set_pixel",
                description=(
                    "Set one LED in the Port B WS2812 strip buffer. Call "
                    "port_b_ws2812_init first. index is 0..255, with the "
                    "effective range bounded by led_count. r, g, and b are "
                    "0..255. By default the color is buffered only; pass "
                    "refresh=true to latch it immediately, or call "
                    "port_b_ws2812_refresh after several buffered updates. "
                    "Port B outputs 3.3 V CMOS data on GPIO 9; older strict "
                    "5 V WS2812 variants may require a level shifter."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "index": {
                            "type": "integer",
                            "description": "LED index (0..255).",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "r": {
                            "type": "integer",
                            "description": "Red 0..255.",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "g": {
                            "type": "integer",
                            "description": "Green 0..255.",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "b": {
                            "type": "integer",
                            "description": "Blue 0..255.",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "refresh": {
                            "type": "boolean",
                            "description": "True to latch the update immediately.",
                            "default": False,
                        },
                    },
                    "required": ["index", "r", "g", "b"],
                },
            ),
            Tool(
                name="port_b_ws2812_set_strip",
                description=(
                    "Set multiple LEDs in the Port B WS2812 strip and refresh "
                    "immediately. Call port_b_ws2812_init first. colors is an "
                    "array of [r,g,b] integer triples applied from LED index 0; "
                    "up to led_count entries are written, extras are ignored, "
                    "and missing trailing entries preserve the previous buffer. "
                    "The firmware validates the full payload before writing. "
                    "Port B outputs 3.3 V CMOS data on GPIO 9; older strict "
                    "5 V WS2812 variants may require a level shifter."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "colors": {
                            "type": "array",
                            "description": (
                                "Array of [r,g,b] triples, each integer 0..255."
                            ),
                            "items": {
                                "type": "array",
                                "items": {
                                    "type": "integer",
                                    "minimum": 0,
                                    "maximum": 255,
                                },
                                "minItems": 3,
                                "maxItems": 3,
                            },
                            "minItems": 1,
                            "maxItems": 256,
                        },
                    },
                    "required": ["colors"],
                },
            ),
            Tool(
                name="port_b_ws2812_refresh",
                description=(
                    "Refresh the Port B WS2812 strip, latching the current "
                    "buffered colors out on CoreS3 HY2.0-4P digital OUTPUT "
                    "GPIO 9. Call port_b_ws2812_init first. Use this after "
                    "one or more port_b_ws2812_set_pixel calls made with "
                    "refresh=false. Port B outputs 3.3 V CMOS data; older "
                    "strict 5 V WS2812 variants may require a level shifter."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="port_b_ws2812_clear",
                description=(
                    "Turn off every LED in the Port B WS2812 strip and "
                    "refresh immediately on CoreS3 HY2.0-4P digital OUTPUT "
                    "GPIO 9. Call port_b_ws2812_init first. This clears the "
                    "driver's per-pixel buffer. Port B outputs 3.3 V CMOS "
                    "data; older strict 5 V WS2812 variants may require a "
                    "level shifter."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="port_c_ws2812_init",
                description=(
                    "Initialize a WS2812-compatible LED strip connected to "
                    "Port C (CoreS3 HY2.0-4P digital OUTPUT, GPIO 17). "
                    "led_count is the number of LEDs in the strip (1..256). "
                    "Repeated calls with the same led_count are no-ops; a "
                    "different led_count rebuilds the strip handle. Port C "
                    "outputs 3.3 V CMOS data on GPIO 17; older strict 5 V "
                    "WS2812 variants may require a level shifter."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "led_count": {
                            "type": "integer",
                            "description": "Number of LEDs in the strip (1..256).",
                            "minimum": 1,
                            "maximum": 256,
                        },
                        "color_order": {
                            "type": "string",
                            "enum": ["grb", "rgb"],
                            "default": "grb",
                            "description": (
                                "Logical LED color order. Use grb for standard "
                                "WS2812/NeoPixel strips, or rgb for RGB-wired "
                                "LEDs; the gateway swaps R/G before forwarding "
                                "colors to the firmware."
                            ),
                        },
                    },
                    "required": ["led_count"],
                },
            ),
            Tool(
                name="port_c_ws2812_set_pixel",
                description=(
                    "Set one LED in the Port C WS2812 strip buffer. Call "
                    "port_c_ws2812_init first. index is 0..255, with the "
                    "effective range bounded by led_count. r, g, and b are "
                    "0..255. By default the color is buffered only; pass "
                    "refresh=true to latch it immediately, or call "
                    "port_c_ws2812_refresh after several buffered updates. "
                    "Port C outputs 3.3 V CMOS data on GPIO 17; older strict "
                    "5 V WS2812 variants may require a level shifter."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "index": {
                            "type": "integer",
                            "description": "LED index (0..255).",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "r": {
                            "type": "integer",
                            "description": "Red 0..255.",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "g": {
                            "type": "integer",
                            "description": "Green 0..255.",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "b": {
                            "type": "integer",
                            "description": "Blue 0..255.",
                            "minimum": 0,
                            "maximum": 255,
                        },
                        "refresh": {
                            "type": "boolean",
                            "description": "True to latch the update immediately.",
                            "default": False,
                        },
                    },
                    "required": ["index", "r", "g", "b"],
                },
            ),
            Tool(
                name="port_c_ws2812_set_strip",
                description=(
                    "Set multiple LEDs in the Port C WS2812 strip and refresh "
                    "immediately. Call port_c_ws2812_init first. colors is an "
                    "array of [r,g,b] integer triples applied from LED index 0; "
                    "up to led_count entries are written, extras are ignored, "
                    "and missing trailing entries preserve the previous buffer. "
                    "The firmware validates the full payload before writing. "
                    "Port C outputs 3.3 V CMOS data on GPIO 17; older strict "
                    "5 V WS2812 variants may require a level shifter."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "colors": {
                            "type": "array",
                            "description": (
                                "Array of [r,g,b] triples, each integer 0..255."
                            ),
                            "items": {
                                "type": "array",
                                "items": {
                                    "type": "integer",
                                    "minimum": 0,
                                    "maximum": 255,
                                },
                                "minItems": 3,
                                "maxItems": 3,
                            },
                            "minItems": 1,
                            "maxItems": 256,
                        },
                    },
                    "required": ["colors"],
                },
            ),
            Tool(
                name="port_c_ws2812_refresh",
                description=(
                    "Refresh the Port C WS2812 strip, latching the current "
                    "buffered colors out on CoreS3 HY2.0-4P digital OUTPUT "
                    "GPIO 17. Call port_c_ws2812_init first. Use this after "
                    "one or more port_c_ws2812_set_pixel calls made with "
                    "refresh=false. Port C outputs 3.3 V CMOS data; older "
                    "strict 5 V WS2812 variants may require a level shifter."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="port_c_ws2812_clear",
                description=(
                    "Turn off every LED in the Port C WS2812 strip and "
                    "refresh immediately on CoreS3 HY2.0-4P digital OUTPUT "
                    "GPIO 17. Call port_c_ws2812_init first. This clears the "
                    "driver's per-pixel buffer. Port C outputs 3.3 V CMOS "
                    "data; older strict 5 V WS2812 variants may require a "
                    "level shifter."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="say",
                description=(
                    "Speak the given text on the device speaker via gateway-side "
                    "TTS. The gateway synthesises audio, encodes it to Opus, "
                    "and pushes frames over the existing WebSocket; the device "
                    "firmware does not change. Engine is selectable via 'voice' "
                    "(default 'voicevox'). If the text contains a supported "
                    "expression emoji, say first switches the avatar face in the "
                    "same call: happy (😊 😄 😀 😁 🙂 😆 🥰 😍 😋 🤗), "
                    "sad (😢 😭 😞 😔 ☹️ 🙁 😿), surprised (😲 😮 😯 😱 🤯), "
                    "embarrassed (😳 😅 🫣), thinking (🤔 🧐 💭). The first "
                    "mapped emoji wins; unmapped emoji do not change the face, "
                    "and emoji never select 'off'. Irodori keeps emoji in the "
                    "TTS input so they can act as voice-style cues. Engines "
                    "without emoji-style support, including VOICEVOX, strip all "
                    "emoji before synthesis; if stripping leaves empty text, the "
                    "face change is still attempted and speech is skipped."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "text": {
                            "type": "string",
                            "description": "Text to speak. Must be non-empty.",
                        },
                        "voice": {
                            "type": "string",
                            "description": (
                                "Engine identifier (e.g. 'voicevox', 'irodori', "
                                "'elevenlabs'). "
                                "Default 'voicevox'."
                            ),
                            "default": "voicevox",
                        },
                        "speaker_id": {
                            "type": "integer",
                            "description": (
                                "Engine-specific numeric speaker identifier "
                                "(e.g. a VOICEVOX speaker ID)."
                            ),
                        },
                        "speaker_name": {
                            "type": "string",
                            "description": (
                                "Engine-specific string speaker/voice "
                                "identifier (e.g. an Edge TTS voice name "
                                "such as 'en-US-AriaNeural'). Distinct from "
                                "'voice', which selects the engine itself "
                                "(e.g. 'edge-tts'); use speaker_name for "
                                "engines whose speaker selector is a string "
                                "name rather than the numeric speaker_id."
                            ),
                        },
                        "reference_audio": {
                            "type": "string",
                            "description": (
                                "Path to a reference audio file used by "
                                "voice-cloning engines (e.g. Irodori). "
                                "Ignored by engines that do not support it."
                            ),
                        },
                    },
                    "required": ["text"],
                },
            ),
            Tool(
                name="listen",
                description=(
                    "Capture a short utterance from the device microphone and "
                    "transcribe it via a gateway-side STT engine (Phase 4, "
                    "Issue #91). The gateway sends a 'listen' notification "
                    "over the existing WebSocket to put the device firmware "
                    "into listening mode, buffers the Opus frames the device "
                    "streams up during the capture window, then decodes and "
                    "transcribes them once the window closes. Requires a "
                    "minimal firmware change to handle the inbound 'listen' "
                    "wire type (paired with this gateway release). Engine is "
                    "selectable via 'engine' (default 'faster-whisper', local). "
                    "Optional 'motion' feedback can switch the avatar to "
                    "'thinking' during capture ('face-only') or tilt the head "
                    "up while preserving yaw ('look-up'). "
                    "Install the relevant extra "
                    "('pip install stackchan-mcp[stt-faster-whisper]' or "
                    "'stt-openai'); calling this tool before an engine is "
                    "registered returns a clear error."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "duration_ms": {
                            "type": "integer",
                            "description": (
                                "Capture window in milliseconds. Clamped to "
                                "[100, 30000]."
                            ),
                            "default": 5000,
                            "minimum": 100,
                            "maximum": 30000,
                        },
                        "engine": {
                            "type": "string",
                            "description": (
                                "Engine identifier (e.g. 'faster-whisper', "
                                "'openai-whisper'). Default 'faster-whisper'."
                            ),
                            "default": "faster-whisper",
                        },
                        "language": {
                            "type": "string",
                            "description": (
                                "ISO 639-1 language code (e.g. 'ja', 'en'). "
                                "Omit this field to use STACKCHAN_LISTEN_LANGUAGE, "
                                "or 'ja' when that env is unset. Do not rely on a "
                                "schema default — many clients send defaults even "
                                "when the caller did not choose a language."
                            ),
                        },
                        "model": {
                            "type": "string",
                            "description": (
                                "Engine-specific model identifier (e.g. "
                                "'base' / 'small' / 'medium' for faster-"
                                "whisper, 'whisper-1' for OpenAI). Engines "
                                "fall back to their default when omitted."
                            ),
                        },
                        "motion": {
                            "type": "string",
                            "enum": ["none", "face-only", "look-up"],
                            "description": (
                                "Optional visible feedback during capture. "
                                "'none' preserves the previous behaviour. "
                                "'face-only' shows the thinking avatar during "
                                "capture and restores idle at the end. "
                                "'look-up' preserves yaw, tilts pitch to "
                                "look_up_pitch, and holds the pose on success."
                            ),
                            "default": "none",
                        },
                        "look_up_pitch": {
                            "type": "number",
                            "description": (
                                "Pitch angle for motion='look-up'. Must be "
                                "between 5 and 85 degrees."
                            ),
                            "default": 50.0,
                            "minimum": 5,
                            "maximum": 85,
                        },
                    },
                },
            ),
            Tool(
                name="beat_mode_start",
                description=(
                    "Start gateway-side beat mode. The gateway reuses the "
                    "existing listen wire path to capture ambient device audio, "
                    "decodes it to 16 kHz mono PCM, estimates beat/BPM locally, "
                    "and drives a free-running beat-synced head sway plus base "
                    "ring LED flash. While active, listen() calls fail fast "
                    "because beat mode owns the microphone capture slot. say() "
                    "is allowed to interrupt; beat mode re-sends listen.start "
                    "after speech or reconnect when audio frames stop arriving. "
                    "Requires the Opus decoder from the STT extra."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "motion_intensity": {
                            "type": "number",
                            "default": 0.5,
                            "minimum": 0,
                            "maximum": 1,
                            "description": (
                                "Head sway intensity. 0 keeps motion near "
                                "center; 1 uses the maximum v1 sway template."
                            ),
                        },
                        "sensitivity": {
                            "type": "number",
                            "default": 0.5,
                            "minimum": 0,
                            "maximum": 1,
                            "description": (
                                "Onset sensitivity for venue tuning. Log-scale "
                                "anchors: 0.0 => min_onset_rms 0.025 (least "
                                "sensitive), 0.5 => 0.004 (default verified on "
                                "device), 1.0 => about 0.001 (most sensitive)."
                            ),
                        },
                        "color": {
                            "type": "array",
                            "items": {
                                "type": "integer",
                                "minimum": 0,
                                "maximum": 255,
                            },
                            "minItems": 3,
                            "maxItems": 3,
                            "description": (
                                "Optional base-ring flash color as [r, g, b]. "
                                "Defaults to cyan-blue."
                            ),
                        },
                        "duration_sec": {
                            "type": "integer",
                            "minimum": 1,
                            "description": (
                                "Optional auto-stop duration in seconds. Omit "
                                "to keep beat mode running until stopped."
                            ),
                        },
                    },
                },
            ),
            Tool(
                name="beat_mode_stop",
                description=(
                    "Stop beat mode, send listen.stop best-effort, and keep "
                    "the last rolling audio buffer available for beat_clip_save "
                    "until the next beat mode start or gateway restart."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="beat_mode_update",
                description=(
                    "Update beat mode VJ parameters without restarting capture: "
                    "motion intensity, onset sensitivity, base-ring flash color, "
                    "blink-rate multiplier, and motion/LED enable toggles."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "motion_intensity": {
                            "type": "number",
                            "minimum": 0,
                            "maximum": 1,
                        },
                        "sensitivity": {
                            "type": "number",
                            "minimum": 0,
                            "maximum": 1,
                            "description": (
                                "Onset sensitivity for venue tuning. Log-scale "
                                "anchors: 0.0 => min_onset_rms 0.025 (least "
                                "sensitive), 0.5 => 0.004 (default verified on "
                                "device), 1.0 => about 0.001 (most sensitive)."
                            ),
                        },
                        "color": {
                            "type": "array",
                            "items": {
                                "type": "integer",
                                "minimum": 0,
                                "maximum": 255,
                            },
                            "minItems": 3,
                            "maxItems": 3,
                        },
                        "blink_rate": {
                            "type": "number",
                            "minimum": 0.25,
                            "maximum": 4,
                            "description": (
                                "LED flash cadence multiplier relative to the "
                                "detected beat period."
                            ),
                        },
                        "motion_enabled": {"type": "boolean"},
                        "led_enabled": {"type": "boolean"},
                    },
                },
            ),
            Tool(
                name="beat_meta_snapshot",
                description=(
                    "Return the latest beat mode snapshot: active state, BPM, "
                    "confidence, last beat/audio monotonic timestamps, capture "
                    "health, rolling-buffer duration, counters, current "
                    "motion/LED parameters, and the active sensitivity with "
                    "its effective min_onset_rms floor. This is a polling "
                    "snapshot; beat mode does not push notifications."
                ),
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="beat_clip_save",
	                description=(
	                    "Save the most recent beat-mode audio window as a WAV file "
	                    "(16 kHz mono signed 16-bit PCM) and return the absolute "
	                    "temp-file path plus actual captured duration. Works while "
	                    "beat mode is active and against the retained buffer after "
	                    "stop, until a new beat mode starts or the gateway restarts. "
	                    "The file persists on disk; the caller is responsible for "
	                    "deleting it when no longer needed."
	                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "seconds": {
                            "type": "number",
                            "default": 10.0,
                            "exclusiveMinimum": 0,
                            "description": (
                                "How many recent seconds to write, capped by "
                                "the rolling capture window."
                            ),
                        },
                    },
                },
            ),
            Tool(
                name="i2c_scan",
                description=(
                    "Scan the external I2C bus on Grove Port A and return "
                    "all 7-bit addresses (probe range 0x08..0x77, "
                    "excluding I2C reserved ranges) that ACK a probe. Use "
                    "this to discover attached M5Stack Unit modules "
                    "(ENV III, ToF, gas sensor, PaHub, etc.). On-board ICs "
                    "on the internal bus are NOT included (this tool "
                    "operates on a physically separate bus). Returns "
                    "{\"ok\": true, \"addresses\": [...]}."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {},
                },
            ),
            Tool(
                name="i2c_read",
                description=(
                    "Read n_bytes from an I2C device at 7-bit address "
                    "`addr` on Grove Port A. Use this for protocols that "
                    "read the device's current register / output without "
                    "a preceding write. For typical 'write register "
                    "address, then read' patterns, use `i2c_write_read` "
                    "instead. Returns "
                    "{\"ok\": true, \"bytes\": [...]} or "
                    "{\"ok\": false, \"error\": \"ESP_ERR_TIMEOUT\"} on NACK. "
                    "Optional `scl_speed_hz` (default 400000) sets the I2C "
                    "clock for this transaction; lower it (e.g. 100000 or "
                    "200000) for slower Units such as the RCWL-9620 "
                    "ultrasonic ranger that fail at 400 kHz with "
                    "ESP_ERR_INVALID_STATE."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "addr": {
                            "type": "integer",
                            "description": (
                                "7-bit I2C address; range 0x08..0x77 "
                                "(I2C reserved ranges excluded — matches "
                                "the i2c_scan probe range)."
                            ),
                            "minimum": 8,
                            "maximum": 119,
                        },
                        "n_bytes": {
                            "type": "integer",
                            "description": "Bytes to read (1..256).",
                            "minimum": 1,
                            "maximum": 256,
                        },
                        "scl_speed_hz": {
                            "type": "integer",
                            "default": 400000,
                            "description": (
                                "I2C clock for this transaction. Default "
                                "400000; lower it (e.g. 100000 or 200000) "
                                "for slower Units such as the RCWL-9620 "
                                "ultrasonic ranger that fail at 400 kHz "
                                "with ESP_ERR_INVALID_STATE."
                            ),
                            "minimum": 100000,
                            "maximum": 1000000,
                        },
                    },
                    "required": ["addr", "n_bytes"],
                },
            ),
            Tool(
                name="i2c_write",
                description=(
                    "Write bytes to an I2C device at 7-bit address `addr` "
                    "on Grove Port A. `bytes` is an array of integers "
                    "(0..255). This tool operates on the external Port A "
                    "bus only; on-board ICs (PMIC, AW9523, touch, etc.) "
                    "on the internal bus are not reachable. Optional "
                    "`scl_speed_hz` (default 400000) sets the I2C clock for "
                    "this transaction; lower it (e.g. 100000 or 200000) for "
                    "slower Units such as the RCWL-9620 ultrasonic ranger "
                    "that fail at 400 kHz with ESP_ERR_INVALID_STATE."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "addr": {
                            "type": "integer",
                            "description": (
                                "7-bit I2C address; range 0x08..0x77 "
                                "(I2C reserved ranges excluded — matches "
                                "the i2c_scan probe range)."
                            ),
                            "minimum": 8,
                            "maximum": 119,
                        },
                        "bytes": {
                            "type": "array",
                            "description": "Bytes to write (each 0..255).",
                            "items": {
                                "type": "integer",
                                "minimum": 0,
                                "maximum": 255,
                            },
                        },
                        "scl_speed_hz": {
                            "type": "integer",
                            "default": 400000,
                            "description": (
                                "I2C clock for this transaction. Default "
                                "400000; lower it (e.g. 100000 or 200000) "
                                "for slower Units such as the RCWL-9620 "
                                "ultrasonic ranger that fail at 400 kHz "
                                "with ESP_ERR_INVALID_STATE."
                            ),
                            "minimum": 100000,
                            "maximum": 1000000,
                        },
                    },
                    "required": ["addr", "bytes"],
                },
            ),
            Tool(
                name="i2c_write_read",
                description=(
                    "Write `write_bytes` to an I2C device at 7-bit address "
                    "`addr` on Grove Port A, then read `n_bytes` back in a "
                    "single Repeated Start transaction. Common 'set "
                    "register pointer, then read' idiom: pass "
                    "write_bytes=[reg_addr] to read from a specific "
                    "register. Optional `scl_speed_hz` (default 400000) "
                    "sets the I2C clock for this transaction; lower it "
                    "(e.g. 100000 or 200000) for slower Units such as the "
                    "RCWL-9620 ultrasonic ranger that fail at 400 kHz with "
                    "ESP_ERR_INVALID_STATE."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "addr": {
                            "type": "integer",
                            "description": (
                                "7-bit I2C address; range 0x08..0x77 "
                                "(I2C reserved ranges excluded — matches "
                                "the i2c_scan probe range)."
                            ),
                            "minimum": 8,
                            "maximum": 119,
                        },
                        "write_bytes": {
                            "type": "array",
                            "description": (
                                "Bytes to write before reading "
                                "(each 0..255)."
                            ),
                            "items": {
                                "type": "integer",
                                "minimum": 0,
                                "maximum": 255,
                            },
                        },
                        "n_bytes": {
                            "type": "integer",
                            "description": "Bytes to read (1..256).",
                            "minimum": 1,
                            "maximum": 256,
                        },
                        "scl_speed_hz": {
                            "type": "integer",
                            "default": 400000,
                            "description": (
                                "I2C clock for this transaction. Default "
                                "400000; lower it (e.g. 100000 or 200000) "
                                "for slower Units such as the RCWL-9620 "
                                "ultrasonic ranger that fail at 400 kHz "
                                "with ESP_ERR_INVALID_STATE."
                            ),
                            "minimum": 100000,
                            "maximum": 1000000,
                        },
                    },
                    "required": ["addr", "write_bytes", "n_bytes"],
                },
            ),
            Tool(
                name="load_avatar_set",
                description=(
                    "Load a dynamic avatar set onto the connected ESP32 "
                    "(Phase 4.5 avatar pipeline). The gateway stages the "
                    "payload on its HTTP server, notifies the device via "
                    "WebSocket, and the device fetches + SHA256-verifies + "
                    "loads it into PSRAM. ``archive_path`` must point to a "
                    "raw RGB565 file on the gateway host: layered mode = "
                    "14 frames (face 6 + eyes 3 + mouth 5) totalling "
                    "537,600 bytes; matrix mode = 90 frames (6 × 3 × 5) "
                    "totalling 3,456,000 bytes. Returns ok / checksum / "
                    "bytes_transferred / error."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "archive_path": {
                            "type": "string",
                            "description": (
                                "Filesystem path on the gateway host to "
                                "the raw RGB565 payload."
                            ),
                        },
                        "mode": {
                            "type": "string",
                            "enum": ["layered", "matrix"],
                            "description": (
                                "'layered' (14 frames, ~525 KB) or "
                                "'matrix' (90 frames, ~3.3 MB)."
                            ),
                        },
                        "timeout": {
                            "type": "number",
                            "description": (
                                "Max seconds to wait for the device's "
                                "avatar_set_loaded reply."
                            ),
                            "default": 60.0,
                            "minimum": 5.0,
                            "maximum": 300.0,
                        },
                    },
                    "required": ["archive_path", "mode"],
                },
            ),
            Tool(
                name="upgrade_firmware",
                description=(
                    "Flash new firmware to the device over Wi-Fi (no USB). "
                    "``firmware_path`` is a local app image on the gateway "
                    "host (``build/xiaozhi.bin`` or the Release asset "
                    "``xiaozhi.bin``; not merged-binary.bin). The gateway "
                    "first checks it is an ESP32-S3 xiaozhi image for this "
                    "board with a valid SHA-256 and that it fits the OTA "
                    "slot, and refuses it otherwise without contacting the "
                    "device. It then serves the file once from a random, "
                    "short-lived URL on the capture port, asks the device "
                    "to download and install it, waits for the reboot and "
                    "reconnect, and confirms the running image. Returns "
                    "ok / before / after (version, compile_time, "
                    "elf_sha256, partition) / error. Takes about a minute."
                ),
                inputSchema={
                    "type": "object",
                    "properties": {
                        "firmware_path": {
                            "type": "string",
                            "description": (
                                "Filesystem path on the gateway host to "
                                "xiaozhi.bin."
                            ),
                        },
                        "timeout": {
                            "type": "number",
                            "description": (
                                "Max seconds to wait for download, reboot "
                                "and reconnect."
                            ),
                            "default": 180.0,
                            "minimum": 60.0,
                            "maximum": 600.0,
                        },
                    },
                    "required": ["firmware_path"],
                },
            ),
        ]
        return _with_device_id_schema(tools)

    @server.call_tool()
    async def call_tool(
        name: str, arguments: dict[str, Any] | None
    ) -> list[TextContent | ImageContent]:
        """Handle a tool call by relaying to ESP32."""
        arguments = arguments or {}
        return await _dispatch_mcp_tool(name, arguments, get_gateway())

    return server


async def run_stdio_server(notify_config: NotifyConfig | None = None) -> None:
    """Run the MCP server on stdio."""
    if notify_config is None:
        notify_config = load_notify_config()
    server = create_server(notify_config=notify_config)
    async with stdio_server() as (read_stream, write_stream):
        logger.info("stdio MCP server starting")
        await server.run(
            read_stream,
            write_stream,
            _create_initialization_options(server, notify_config),
        )
