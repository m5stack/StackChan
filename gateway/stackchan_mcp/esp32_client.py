"""ESP32 connection manager.

Acts as a WebSocket server that ESP32 connects TO,
and as an MCP client that sends commands TO the ESP32.
"""

from __future__ import annotations

import asyncio
from collections.abc import Sequence
import json
import logging
import os
import time
import uuid
from typing import Any

import websockets
import websockets.exceptions
from websockets.asyncio.server import ServerConnection

from .audio_input_hook import push_audio_capture
from .audio_stream import (
    handle_audio_frame,
    is_recording,
    is_recording_session,
    start_recording,
    stop_recording,
)
from .notify_config import (
    DEFAULT_MESSAGE_TEMPLATES,
    NotifyConfig,
    load_notify_config,
    render_template,
)
from .protocol import HelloResponse, make_mcp_message, parse_jsonrpc_response

logger = logging.getLogger(__name__)

# Timeout for waiting for ESP32 responses
RESPONSE_TIMEOUT = 10.0
WEBSOCKET_PING_INTERVAL_S = 20
WEBSOCKET_PING_TIMEOUT_S = 20

ToolCall = tuple[str, dict[str, Any]]
ToolCallResult = tuple[Any, dict[str, Any] | None]

_SET_AVATAR_TOOL = "self.display.set_avatar"

_TOOL_LANES = {
    "self.robot.": "servo",
    "self.wifi.": "wifi",
    "self.led.": "led",
    "self.port_b.": "port_b",
    "self.port_c.": "port_c",
    "self.display.": "avatar",
    "self.screen.": "display",
    "self.audio_speaker.": "audio",
    "self.camera.": "camera",
    "self.touch.": "touch",
    "self.get_device_status": "status",
}

_TOOL_LANE_NAMES = (
    "servo",
    "wifi",
    "led",
    "port_b",
    "port_c",
    "avatar",
    "display",
    "audio",
    "camera",
    "touch",
    "status",
    "default",
)


def _hardware_lane(tool_name: str) -> str:
    """Return the hardware lane used for per-peripheral dispatch ordering."""
    for prefix, lane in _TOOL_LANES.items():
        if tool_name.startswith(prefix):
            return lane
    return "default"


def _make_tool_lane_locks() -> dict[str, asyncio.Lock]:
    """Create a fresh per-lane lock map for one device connection."""
    return {name: asyncio.Lock() for name in _TOOL_LANE_NAMES}


def _retrieve_future_exception(future: asyncio.Future[Any]) -> None:
    """Mark a completed Future exception as observed, if it has one."""
    if future.done() and not future.cancelled():
        future.exception()


def _close_frame_fields(close_frame: Any | None) -> tuple[int | None, str | None]:
    """Return log-friendly code/reason fields for a WebSocket close frame."""
    if close_frame is None:
        return None, None
    return getattr(close_frame, "code", None), getattr(close_frame, "reason", None)


def _format_elapsed_s(started_at: float | None, now: float) -> str:
    """Format elapsed monotonic seconds for stable, compact log output."""
    if started_at is None:
        return "None"
    return f"{now - started_at:.3f}"


def _monotonic() -> float:
    """Return monotonic time for connection observability."""
    return time.monotonic()


def _log_disconnect_details(
    *,
    device_id: str,
    close_class: str,
    rcvd_code: int | None,
    rcvd_reason: str | None,
    sent_code: int | None,
    sent_reason: str | None,
    connected_at: float,
    last_frame_received_at: float | None,
) -> None:
    disconnected_at = _monotonic()
    logger.info(
        "ESP32 disconnected: device=%s close_class=%s "
        "rcvd_code=%s rcvd_reason=%r sent_code=%s sent_reason=%r "
        "last_frame_age_s=%s lifetime_s=%s",
        device_id,
        close_class,
        rcvd_code,
        rcvd_reason,
        sent_code,
        sent_reason,
        _format_elapsed_s(last_frame_received_at, disconnected_at),
        _format_elapsed_s(connected_at, disconnected_at),
    )


class ESP32Connection:
    """Manages a single ESP32 device connection."""

    def __init__(self, ws: ServerConnection, session_id: str):
        self._ws = ws
        self.session_id = session_id
        self.device_id: str = "unknown"
        self.tools: list[dict[str, Any]] = []
        self._request_id = 0
        self._pending: dict[int, asyncio.Future[dict[str, Any]]] = {}
        self._connected = True
        self._initialized = False
        self._tools_discovered = False
        self._avatar_render_sent = False
        # Phase 4.5 avatar: pending load_avatar_set calls waiting for the
        # device's `avatar_set_loaded` reply. Keyed by expected checksum
        # so that overlapping fetches (different sets) can be discriminated.
        self._avatar_set_waiters: dict[str, asyncio.Future[dict[str, Any]]] = {}
        # Device-declared WebSocket protocol version (from the hello
        # message). Defaults to 1, which matches the firmware's default
        # (firmware/main/protocols/websocket_protocol.h: ``version_ = 1``)
        # and the audio framing this gateway emits today (raw Opus
        # payload). v2/v3 add a BinaryProtocol header that this gateway
        # does not yet wrap — see Issue follow-up to #70.
        self.protocol_version: int = 1
        # Wall-clock time when this connection was registered (hello).
        # Used by list_devices; None until the manager registers us.
        self.connected_at: float | None = None
        # Per-device serialisation for TTS / STT audio path. Concurrent
        # ``say()`` / ``listen()`` on the same device must not interleave
        # frames or yank the firmware out of speaking mid-utterance.
        # See ESP32Manager docstring history: locks live on the
        # connection so multi-device routing isolates audio paths.
        self._tts_lock = asyncio.Lock()
        self._listen_lock = self._tts_lock
        # Per-hardware-lane locks: calls on different peripherals may
        # overlap; same-lane calls stay ordered.
        self._tool_lane_locks = _make_tool_lane_locks()

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def initialized(self) -> bool:
        return self._initialized

    @property
    def tools_discovered(self) -> bool:
        return self._tools_discovered

    @property
    def local_host(self) -> str | None:
        """Gateway IP this device's WebSocket reached (None if unknown)."""
        try:
            addr = self._ws.local_address
        except Exception:
            return None
        if isinstance(addr, (tuple, list)) and addr and isinstance(addr[0], str):
            return addr[0]
        return None

    @property
    def avatar_render_sent(self) -> bool:
        return self._avatar_render_sent

    @property
    def tts_lock(self) -> asyncio.Lock:
        """Lock guarding the TTS send sequence for this device."""
        return self._tts_lock

    @property
    def listen_lock(self) -> asyncio.Lock:
        """Lock guarding the STT capture sequence for this device.

        Shares the TTS lock: the firmware aborts in-flight TTS on
        listen.start, so the audio path is one serialised resource.
        """
        return self._listen_lock

    def _next_id(self) -> int:
        self._request_id += 1
        return self._request_id

    async def send_mcp_request(
        self, method: str, params: dict[str, Any]
    ) -> tuple[Any, dict[str, Any] | None]:
        """Send an MCP request to ESP32 and wait for response.

        Returns (result, error).
        """
        if not self._connected:
            return None, {"code": -32000, "message": "ESP32 not connected"}

        req_id = self._next_id()
        message = make_mcp_message(self.session_id, method, params, req_id)

        future: asyncio.Future[dict[str, Any]] = asyncio.get_event_loop().create_future()
        self._pending[req_id] = future

        try:
            await self._ws_send(json.dumps(message))
            response = await asyncio.wait_for(future, timeout=RESPONSE_TIMEOUT)
            return parse_jsonrpc_response(response)
        except asyncio.CancelledError:
            self._pending.pop(req_id, None)
            raise
        except asyncio.TimeoutError:
            self._pending.pop(req_id, None)
            return None, {"code": -32000, "message": f"Timeout waiting for ESP32 response (method={method})"}
        except Exception as exc:
            self._pending.pop(req_id, None)
            _retrieve_future_exception(future)
            return None, {"code": -32000, "message": f"ESP32 communication error: {exc}"}

    async def initialize(self, vision_url: str = "", vision_token: str = "") -> bool:
        """Send MCP initialize to ESP32."""
        capabilities: dict[str, Any] = {}
        if vision_url:
            vision: dict[str, Any] = {"url": vision_url}
            if vision_token:
                vision["token"] = vision_token
            capabilities["vision"] = vision
        result, error = await self.send_mcp_request("initialize", {"capabilities": capabilities})
        if error:
            logger.error("ESP32 initialize failed: %s", error)
            return False

        logger.info(
            "ESP32 initialized: protocol=%s server=%s",
            result.get("protocolVersion", "?"),
            result.get("serverInfo", {}),
        )
        self._initialized = True
        return True

    async def discover_tools(self) -> list[dict[str, Any]]:
        """Discover tools available on ESP32."""
        all_tools: list[dict[str, Any]] = []
        cursor = ""
        self._tools_discovered = False
        discovered = False

        while True:
            params: dict[str, Any] = {"cursor": cursor}
            result, error = await self.send_mcp_request("tools/list", params)

            if error:
                logger.error("tools/list failed: %s", error)
                self.tools = all_tools
                break

            tools = result.get("tools", [])
            all_tools.extend(tools)

            next_cursor = result.get("nextCursor", "")
            if not next_cursor:
                discovered = True
                break
            cursor = next_cursor

        self.tools = all_tools
        self._tools_discovered = discovered
        logger.info("Discovered %d tools on ESP32", len(all_tools))
        return all_tools

    async def call_tool(
        self, name: str, arguments: dict[str, Any]
    ) -> tuple[Any, dict[str, Any] | None]:
        """Call a tool on ESP32."""
        if name == _SET_AVATAR_TOOL:
            self._avatar_render_sent = True
        return await self.send_mcp_request(
            "tools/call", {"name": name, "arguments": arguments}
        )

    async def send_avatar_set_fetch(
        self,
        url: str,
        token: str,
        mode: str,
        checksum: str,
        expected_size: int,
        timeout: float = 60.0,
    ) -> dict[str, Any]:
        """Send avatar_set_fetch notification and wait for avatar_set_loaded.

        Returns the device's reply dict ({ok, checksum, error}). Returns a
        synthesized {ok: False, error: ...} dict on timeout or send failure.
        """
        if not self._connected:
            return {"ok": False, "checksum": checksum, "error": "not_connected"}

        future: asyncio.Future[dict[str, Any]] = asyncio.get_event_loop().create_future()
        # Last-writer-wins on duplicate checksum: cancel the previous waiter
        # so the same set being re-pushed doesn't strand callers.
        previous = self._avatar_set_waiters.pop(checksum, None)
        if previous is not None and not previous.done():
            previous.cancel()
        self._avatar_set_waiters[checksum] = future

        msg = {
            "type": "avatar_set_fetch",
            "url": url,
            "token": token,
            "mode": mode,
            "checksum": checksum,
            "expected_size": expected_size,
        }
        try:
            await self._ws.send(json.dumps(msg))
            result = await asyncio.wait_for(future, timeout=timeout)
            return result
        except asyncio.TimeoutError:
            self._avatar_set_waiters.pop(checksum, None)
            return {"ok": False, "checksum": checksum, "error": "device_timeout"}
        except asyncio.CancelledError:
            return {"ok": False, "checksum": checksum, "error": "superseded"}
        except ConnectionError:
            self._avatar_set_waiters.pop(checksum, None)
            return {"ok": False, "checksum": checksum, "error": "disconnected"}
        except Exception as exc:
            self._avatar_set_waiters.pop(checksum, None)
            return {"ok": False, "checksum": checksum, "error": f"send_failed: {exc}"}

    def handle_avatar_set_loaded(self, payload: dict[str, Any]) -> None:
        """Resolve a pending send_avatar_set_fetch by checksum."""
        checksum = payload.get("checksum", "")
        future = self._avatar_set_waiters.pop(checksum, None)
        if future is not None and not future.done():
            future.set_result(payload)
        else:
            logger.warning(
                "avatar_set_loaded for unknown checksum=%s (no pending waiter)",
                checksum,
            )

    def handle_response(self, payload: dict[str, Any]) -> None:
        """Handle an incoming MCP response from ESP32."""
        req_id = payload.get("id")
        if req_id is not None and req_id in self._pending:
            future = self._pending.pop(req_id)
            if not future.done():
                future.set_result(payload)
        else:
            # Notification (no id) — log and discard for now
            method = payload.get("method", "")
            logger.info("ESP32 notification: %s", method)

    async def _ws_send(self, payload: bytes | str) -> None:
        """Send a payload, translating websockets errors to ConnectionError.

        The ``websockets`` library raises its own exception hierarchy
        (``ConnectionClosed`` and friends), which is *not* a subclass
        of the built-in :class:`ConnectionError`. Without translation
        the orchestrator's ``except ConnectionError`` filter — and the
        MCP handler's ``except RuntimeError`` filter — would let those
        errors leak as raw tracebacks into the MCP transport, breaking
        the say() tool's clean error JSON contract on mid-stream
        disconnect.
        """
        try:
            await self._ws.send(payload)
        except (
            websockets.exceptions.ConnectionClosed,
            OSError,
        ) as exc:
            # Mark the connection dead so subsequent calls fail fast
            # rather than each one re-discovering the broken socket.
            self.disconnect()
            raise ConnectionError(f"WebSocket send failed: {exc}") from exc

    async def send_audio_frame(self, opus_frame: bytes) -> None:
        """Send a single Opus frame to the ESP32 as a WebSocket binary frame.

        The device's ``OnData`` handler (firmware/main/protocols/
        websocket_protocol.cc) treats every binary frame as an Opus
        audio payload to feed into its decoder, so this method is the
        TTS pipeline's egress point.
        """
        if not self._connected:
            raise ConnectionError("ESP32 not connected")
        await self._ws_send(opus_frame)

    async def send_tts_state(self, state: str) -> None:
        """Send a TTS state notification (``start`` / ``stop`` / ...).

        The device's :func:`Application::OnIncomingJson` translates
        ``{"type":"tts","state":"start"}`` into
        :data:`kDeviceStateSpeaking`, which is the gate for
        :func:`OnIncomingAudio` pushing packets into the decode queue
        (see ``firmware/main/application.cc``). Without bracketing the
        audio frames in start/stop, the device drops them on the floor
        and the speaker stays silent — the TTS tool returns success
        without anything actually playing.
        """
        if not self._connected:
            raise ConnectionError("ESP32 not connected")
        message = {
            "session_id": self.session_id,
            "type": "tts",
            "state": state,
        }
        await self._ws_send(json.dumps(message))

    async def send_listen_state(
        self,
        state: str,
        mode: str = "manual",
        profile: str = "voice",
    ) -> None:
        """Send a listen state notification (``start`` / ``stop``).

        Server-driven counterpart to the device's existing
        :func:`Protocol::SendStartListening` (Issue #91). The
        firmware's :func:`Application::OnIncomingJson` dispatches
        ``state: "start"`` to :func:`Application::StartListening` and
        ``state: "stop"`` to :func:`Application::StopListening`.

        ``mode`` is currently accepted only for ``state="start"`` and is
        carried on the wire for forward-compatibility — the firmware
        accepts but ignores it in Phase 1 because
        :func:`HandleStartListeningEvent` unconditionally enters
        ``kListeningModeManualStop`` (the gateway controls the stop
        boundary explicitly).

        ``profile`` selects the firmware microphone capture source for
        ``state="start"``. The default ``"voice"`` profile is omitted
        from the JSON to keep the wire shape compatible with older logs
        and firmware. Beat mode uses ``"raw"`` to bypass the device-side
        speech AFE path.
        """
        if not self._connected:
            raise ConnectionError("ESP32 not connected")
        message: dict[str, Any] = {
            "session_id": self.session_id,
            "type": "listen",
            "state": state,
        }
        if state == "start":
            message["mode"] = mode
            if profile != "voice":
                message["profile"] = profile
        await self._ws_send(json.dumps(message))

    def disconnect(self) -> None:
        """Mark connection as disconnected."""
        self._connected = False
        self._initialized = False
        # Cancel all pending futures
        for future in self._pending.values():
            if not future.done():
                future.set_exception(ConnectionError("ESP32 disconnected"))
        self._pending.clear()
        for future in self._avatar_set_waiters.values():
            if not future.done():
                future.set_exception(ConnectionError("ESP32 disconnected"))
        self._avatar_set_waiters.clear()


class ESP32Manager:
    """Manages ESP32 device connections.

    Runs a WebSocket server that ESP32 devices connect to.
    Holds multiple concurrent connections keyed by Device-Id, with
    optional default-device routing for callers that omit device_id.
    """

    def __init__(self, notify_config: NotifyConfig | None = None):
        # Active connections keyed by Device-Id header (MAC from firmware).
        self._connections: dict[str, ESP32Connection] = {}
        # Default routing target when tool callers omit device_id.
        # Initially the first device that completes hello; changeable via
        # set_default_device.
        self._default_device_id: str | None = None
        # Mirror of the default connection for backward-compatible
        # single-device call sites (orchestrators, older tests that assign
        # ``mgr._connection`` directly). Always kept in sync with the
        # registry when connections are registered/removed through
        # manager APIs.
        self._connection: ESP32Connection | None = None
        self._server: Any = None
        self._lock = asyncio.Lock()
        self._notify_config = notify_config or load_notify_config()
        self._init_tasks: list[asyncio.Task] = []
        self._vision_url: str = ""
        self._vision_token: str = ""
        # Fallback audio-path locks used when no device is connected
        # (or a test fake lacks per-connection locks). Real devices use
        # :attr:`ESP32Connection._tts_lock` / ``_listen_lock`` so each
        # unit serialises its own audio path independently.
        self._tts_lock = asyncio.Lock()
        # Inbound STT capture (Issue #91) shares the TTS lock rather
        # than running on a separate one. The firmware's
        # ``HandleStartListeningEvent`` aborts any in-flight TTS when
        # a listen.start arrives mid-speaking (state ==
        # ``kDeviceStateSpeaking`` → ``AbortSpeaking`` →
        # ``SetListeningMode(kListeningModeManualStop)``), so two
        # operations on the same device's audio path would
        # otherwise step on each other. Per-device locks live on
        # :class:`ESP32Connection`; this manager-level pair is only
        # the no-device fallback for property access.
        self._listen_lock = self._tts_lock
        # Device-driven listen capture (= wake word / button / LCD touch
        # paths on the firmware side that call ToggleChatState /
        # WakeWordInvoke / StartListening without an MCP-driven
        # ``listen()`` tool call). When ``_audio_hook_url`` is set, we
        # open the shared audio_stream recording slot on inbound
        # ``{"type":"listen","state":"start"}`` and forward the buffered
        # Opus frames to the hook on the matching ``"stop"`` message.
        # See :mod:`stackchan_mcp.audio_input_hook` for the rationale
        # and protocol details.
        self._audio_hook_url: str = ""
        self._audio_hook_token: str = ""
        # session_id (when device-driven listen has the recording slot
        # open) or None. Storing the session_id rather than a plain bool
        # lets the per-handler disconnect cleanup confirm it still owns
        # the recording before tearing it down — otherwise a stale
        # disconnect can clobber the active buffer of an unrelated
        # session (e.g., a fresh reconnection or an MCP-driven listen()
        # that already took the slot).
        self._device_driven_session_id: str | None = None
        # Fallback lane locks for test fakes that inject connections
        # without a per-device ``_tool_lane_locks`` map. Real
        # ESP32Connection instances carry their own locks.
        self._tool_lane_locks = _make_tool_lane_locks()

    def set_notify_config(self, notify_config: NotifyConfig) -> None:
        """Replace the startup notification config used for future events."""
        self._notify_config = notify_config

    def _sync_default_connection(self) -> None:
        """Refresh ``_connection`` from ``_default_device_id`` / registry."""
        if self._default_device_id is not None:
            conn = self._connections.get(self._default_device_id)
            if conn is not None:
                self._connection = conn
                return
        # Default id missing or stale — pick first remaining, if any.
        if self._connections:
            self._default_device_id = next(iter(self._connections))
            self._connection = self._connections[self._default_device_id]
        else:
            self._default_device_id = None
            self._connection = None

    def _register_connection(
        self, connection: ESP32Connection, device_id: str
    ) -> None:
        """Register or replace a connection for ``device_id``.

        Same device_id replaces the previous socket; different device_ids
        coexist. First registered device becomes the default when none is set.
        Caller must hold ``self._lock`` or accept races only in tests.
        """
        connection.device_id = device_id
        if connection.connected_at is None:
            connection.connected_at = time.time()
        existing = self._connections.get(device_id)
        if existing is not None and existing is not connection and existing.connected:
            logger.warning(
                "Replacing existing ESP32 connection for device=%s", device_id
            )
            existing.disconnect()
        self._connections[device_id] = connection
        if self._default_device_id is None:
            self._default_device_id = device_id
        if self._default_device_id == device_id:
            self._connection = connection

    def _unregister_connection(
        self, connection: ESP32Connection, device_id: str
    ) -> None:
        """Drop ``connection`` from the registry if it is still current."""
        current = self._connections.get(device_id)
        if current is connection:
            del self._connections[device_id]
            if self._default_device_id == device_id:
                self._sync_default_connection()
            elif self._connection is connection:
                self._connection = None

    def list_devices(self) -> list[dict[str, Any]]:
        """Return connected device_ids with connection metadata."""
        devices: list[dict[str, Any]] = []
        for device_id, conn in self._connections.items():
            if not conn.connected:
                continue
            devices.append(
                {
                    "device_id": device_id,
                    "connected_at": conn.connected_at,
                    "session_id": conn.session_id,
                    "initialized": conn.initialized,
                    "is_default": device_id == self._default_device_id,
                }
            )
        return devices

    def set_default_device(self, device_id: str) -> dict[str, Any]:
        """Switch the default routing target to ``device_id``.

        Returns a result dict; never raises for unknown ids.
        """
        conn = self._connections.get(device_id)
        if conn is None or not conn.connected:
            return {
                "ok": False,
                "error": f"Unknown or disconnected device_id: {device_id}",
            }
        self._default_device_id = device_id
        self._connection = conn
        return {"ok": True, "default_device_id": device_id}

    def _resolve_connection(
        self, device_id: str | None
    ) -> tuple[ESP32Connection | None, dict[str, Any] | None]:
        """Pick a connected, initialized target for a tool call.

        ``device_id is None`` → default destination (backward compatible).
        Unknown / disconnected ids return an error dict, never raise.
        """
        if device_id is not None:
            conn = self._connections.get(device_id)
            if conn is None or not conn.connected:
                return None, {
                    "code": -32000,
                    "message": f"Unknown or disconnected device_id: {device_id}",
                }
            if not conn.initialized:
                return None, {"code": -32000, "message": "ESP32 not initialized"}
            return conn, None

        conn = self._connection
        if conn is None or not conn.connected:
            # Registry may have devices even if _connection was cleared
            # by a legacy assignment; try default id / first live one.
            if self._default_device_id:
                conn = self._connections.get(self._default_device_id)
            if conn is None or not conn.connected:
                for candidate in self._connections.values():
                    if candidate.connected:
                        conn = candidate
                        break
            if conn is None or not conn.connected:
                return None, {
                    "code": -32000,
                    "message": "No ESP32 device connected",
                }
        if not conn.initialized:
            return None, {"code": -32000, "message": "ESP32 not initialized"}
        return conn, None

    def _lane_lock_for(
        self, connection: ESP32Connection, lane: str
    ) -> asyncio.Lock:
        """Return the lane lock for ``connection``, with manager fallback."""
        locks = getattr(connection, "_tool_lane_locks", None)
        if isinstance(locks, dict) and lane in locks:
            return locks[lane]
        return self._tool_lane_locks[lane]

    @property
    def device_connected(self) -> bool:
        if self._connection is not None and self._connection.connected:
            return True
        return any(c.connected for c in self._connections.values())

    @property
    def connection(self) -> ESP32Connection | None:
        return self._connection

    @property
    def default_device_id(self) -> str | None:
        return self._default_device_id

    @property
    def tts_lock(self) -> asyncio.Lock:
        """Lock guarding the TTS send sequence for the default device.

        Proxies to the default connection's lock when one is present so
        multi-device audio paths stay isolated; falls back to the
        manager-level lock when no device is attached.
        """
        conn = self._connection
        if conn is not None:
            lock = getattr(conn, "tts_lock", None)
            if lock is not None:
                return lock
        return self._tts_lock

    @property
    def listen_lock(self) -> asyncio.Lock:
        """Lock guarding the STT capture sequence for the default device.

        Shares the default device's TTS lock (see
        :class:`ESP32Connection`); falls back to the manager-level pair
        when no device is attached.
        """
        conn = self._connection
        if conn is not None:
            lock = getattr(conn, "listen_lock", None)
            if lock is not None:
                return lock
        return self._listen_lock

    def connection_for(self, device_id: str | None = None) -> ESP32Connection | None:
        """Return the connection for ``device_id`` (default device when None).

        ``device_id=None`` returns exactly what the ``connection``
        property returns today. An explicit id returns the matching
        connected connection, or ``None`` if unknown/disconnected —
        never raises.
        """
        if device_id is None:
            return self._connection
        conn = self._connections.get(device_id)
        return conn if conn is not None and conn.connected else None

    def tts_lock_for(self, device_id: str | None = None) -> asyncio.Lock:
        """Return the TTS lock for ``device_id`` (default device when None).

        Two different connected devices always get two different
        ``asyncio.Lock`` objects; the same device consistently gets the
        same lock.
        """
        conn = self.connection_for(device_id)
        if conn is not None:
            lock = getattr(conn, "tts_lock", None)
            if lock is not None:
                return lock
        return self._tts_lock

    def listen_lock_for(self, device_id: str | None = None) -> asyncio.Lock:
        """Return the STT lock for ``device_id`` (default device when None)."""
        conn = self.connection_for(device_id)
        if conn is not None:
            lock = getattr(conn, "listen_lock", None)
            if lock is not None:
                return lock
        return self._listen_lock

    def _target_connection(self, device_id: str | None) -> ESP32Connection:
        """Resolve a send target, raising ``ConnectionError`` cleanly.

        ``device_id=None`` reproduces the exact pre-phase2 no-arg
        behaviour (checks only ``self._connection``). An explicit id
        goes through :meth:`_resolve_connection`'s phase1 semantics and
        never lets an unknown/unconnected id crash the caller with an
        uncaught exception — it raises a plain ``ConnectionError``
        instead, which orchestrator call sites already translate to a
        clean ``RuntimeError``.
        """
        if device_id is None:
            if not self._connection or not self._connection.connected:
                raise ConnectionError("No ESP32 device connected")
            return self._connection
        conn, error = self._resolve_connection(device_id)
        if error is not None:
            raise ConnectionError(str(error.get("message", error)))
        assert conn is not None
        return conn

    async def start(
        self,
        host: str = "0.0.0.0",
        port: int = 8765,
        vision_url: str = "",
        vision_token: str = "",
        audio_hook_url: str = "",
        audio_hook_token: str = "",
    ) -> None:
        """Start the WebSocket server for ESP32 connections."""
        self._vision_url = vision_url
        self._vision_token = vision_token
        self._audio_hook_url = audio_hook_url
        self._audio_hook_token = audio_hook_token
        if audio_hook_url:
            logger.info(
                "Device-driven listen capture enabled (audio hook %s)",
                audio_hook_url,
            )
        logger.info(
            "ESP32 WebSocket server starting on ws://%s:%d "
            "ping_interval=%s ping_timeout=%s",
            host,
            port,
            WEBSOCKET_PING_INTERVAL_S,
            WEBSOCKET_PING_TIMEOUT_S,
        )
        self._server = await websockets.serve(
            self._handler,
            host,
            port,
            process_request=self._check_auth,
            ping_interval=WEBSOCKET_PING_INTERVAL_S,
            ping_timeout=WEBSOCKET_PING_TIMEOUT_S,
        )

    async def stop(self) -> None:
        """Stop the WebSocket server."""
        # Cancel any pending initialization tasks
        for task in self._init_tasks:
            task.cancel()
        self._init_tasks.clear()

        if self._server:
            self._server.close()
            await self._server.wait_closed()
            self._server = None

    def _check_auth(
        self, connection: ServerConnection, request: websockets.http11.Request
    ) -> None | websockets.http11.Response:
        """Validate Bearer token.

        websockets 16+ passes (connection, request) to process_request.
        """
        expected = os.getenv("STACKCHAN_TOKEN") or os.getenv("BEARER_TOKEN")
        if not expected:
            logger.warning("STACKCHAN_TOKEN not set — accepting all connections")
            return None

        auth = request.headers.get("Authorization", "")
        if auth == f"Bearer {expected}":
            return None

        logger.warning("ESP32 auth rejected")
        return websockets.http11.Response(
            401, "Unauthorized", websockets.datastructures.Headers()
        )

    async def _handler(self, ws: ServerConnection) -> None:
        """Handle an incoming ESP32 WebSocket connection.

        Architecture: the message read loop runs continuously, dispatching
        MCP responses to pending futures. Initialization (initialize + tools/list)
        runs as a separate task so it doesn't block the read loop.
        """
        session_id = str(uuid.uuid4())
        device_id = (
            ws.request.headers.get("Device-Id", "unknown") if ws.request else "unknown"
        )
        logger.info("ESP32 connecting: device=%s", device_id)

        connection = ESP32Connection(ws, session_id)
        connection.device_id = device_id
        connected_at = _monotonic()
        last_frame_received_at: float | None = None
        disconnect_logged = False

        try:
            async for message in ws:
                last_frame_received_at = _monotonic()
                if isinstance(message, bytes):
                    # Binary = audio frame. Forward to the audio_stream
                    # module which buffers it for STT capture (Issue
                    # #91) when a recording slot is open, or discards
                    # it otherwise. Only protocol v1 is supported on
                    # the inbound side today; the orchestrator gates
                    # listen() on protocol_version=1 so v2/v3 frames
                    # cannot reach this point with recording active.
                    await handle_audio_frame(message, session_id)
                    continue

                try:
                    data = json.loads(message)
                except json.JSONDecodeError:
                    logger.warning("Invalid JSON from ESP32: %s", str(message)[:100])
                    continue

                msg_type = data.get("type", "")

                if msg_type == "hello":
                    # ESP32 hello handshake
                    features = data.get("features", {})
                    if not features.get("mcp"):
                        logger.warning("ESP32 does not support MCP, rejecting")
                        await ws.close()
                        return

                    # Capture the device's WebSocket protocol version
                    # so callers (e.g. the TTS pipeline) can decide
                    # whether their wire format is compatible. The
                    # firmware accepts raw Opus only on v1; v2/v3 wrap
                    # the payload in a BinaryProtocol header.
                    raw_version = data.get("version", 1)
                    try:
                        connection.protocol_version = int(raw_version)
                    except (TypeError, ValueError):
                        connection.protocol_version = 1
                    if connection.protocol_version != 1:
                        logger.warning(
                            "ESP32 negotiated WebSocket protocol "
                            "version=%s; the gateway emits raw Opus "
                            "binary frames matching v1 only. TTS "
                            "calls (say) will be blocked at the "
                            "orchestrator until v2/v3 BinaryProtocol "
                            "header wrapping is implemented",
                            connection.protocol_version,
                        )

                    # Send hello response
                    resp = HelloResponse(session_id=session_id)
                    await ws.send(resp.model_dump_json())

                    # Register connection. Multiple device_ids coexist;
                    # only the same device_id replaces its previous socket.
                    async with self._lock:
                        self._register_connection(connection, device_id)

                    # Start initialization as a separate task so the read loop
                    # continues to pump messages (responses to initialize/tools_list)
                    task = asyncio.create_task(self._init_device(connection, device_id))
                    self._init_tasks.append(task)
                    task.add_done_callback(lambda t: self._init_tasks.remove(t) if t in self._init_tasks else None)

                elif msg_type == "mcp":
                    # MCP response from ESP32
                    payload = data.get("payload", {})
                    connection.handle_response(payload)

                elif msg_type == "avatar_set_loaded":
                    # Phase 4.5 avatar (saiverse-stackchan-addon): device
                    # reports the result of a load_avatar_set fetch (see
                    # docs/intent/stackchan_avatar_pipeline.md §C-3 in
                    # the SAIVerse repository).
                    connection.handle_avatar_set_loaded(data)

                elif msg_type == "stackchan-event":
                    await self._emit_stackchan_event(data)

                elif msg_type == "listen":
                    # Device-driven listening start/stop notification
                    # (wake word, button press, LCD touch — anything
                    # that calls Application::ToggleChatState /
                    # WakeWordInvoke / StartListening on the firmware
                    # side). The MCP-driven listen() tool sends the
                    # same wire format in the reverse direction and
                    # already opens its own recording slot via the STT
                    # orchestrator, so we only act when the device
                    # initiated the capture AND an audio hook URL is
                    # configured to receive the result. See
                    # :mod:`stackchan_mcp.audio_input_hook` for the
                    # forwarding pipeline.
                    state = data.get("state", "")
                    if state == "start":
                        if not self._audio_hook_url:
                            logger.debug(
                                "device-driven listen.start session=%s "
                                "ignored (STACKCHAN_AUDIO_HOOK_URL not "
                                "configured)",
                                session_id,
                            )
                        elif is_recording():
                            # An MCP-driven listen() already owns the
                            # recording slot; let it complete rather
                            # than corrupting its buffer.
                            logger.debug(
                                "device-driven listen.start session=%s "
                                "ignored (MCP-driven recording active)",
                                session_id,
                            )
                        else:
                            start_recording(session_id)
                            self._device_driven_session_id = session_id
                            logger.info(
                                "device-driven listen started: "
                                "session=%s mode=%s",
                                session_id, data.get("mode", ""),
                            )
                    elif state == "stop":
                        if self._device_driven_session_id == session_id:
                            self._device_driven_session_id = None
                            frames = stop_recording()
                            logger.info(
                                "device-driven listen stopped: "
                                "session=%s frames=%d",
                                session_id, len(frames),
                            )
                            # Push asynchronously so the WebSocket read
                            # loop is not blocked by the HTTP POST
                            # round-trip. The task is fire-and-forget;
                            # failures are logged inside
                            # push_audio_capture and do not propagate.
                            asyncio.create_task(
                                push_audio_capture(
                                    self._audio_hook_url,
                                    self._audio_hook_token,
                                    frames,
                                    session_id=session_id,
                                )
                            )
                    else:
                        logger.debug(
                            "listen message with unknown state=%r "
                            "session=%s",
                            state, session_id,
                        )

                else:
                    logger.debug("ESP32 message type=%s (ignored)", msg_type)

            if not disconnect_logged:
                _log_disconnect_details(
                    device_id=device_id,
                    close_class="GracefulClose",
                    rcvd_code=getattr(ws, "close_code", None),
                    rcvd_reason=getattr(ws, "close_reason", None),
                    sent_code=None,
                    sent_reason=None,
                    connected_at=connected_at,
                    last_frame_received_at=last_frame_received_at,
                )
                disconnect_logged = True

        except websockets.exceptions.ConnectionClosed as exc:
            rcvd_code, rcvd_reason = _close_frame_fields(exc.rcvd)
            sent_code, sent_reason = _close_frame_fields(exc.sent)
            _log_disconnect_details(
                device_id=device_id,
                close_class=exc.__class__.__name__,
                rcvd_code=rcvd_code,
                rcvd_reason=rcvd_reason,
                sent_code=sent_code,
                sent_reason=sent_reason,
                connected_at=connected_at,
                last_frame_received_at=last_frame_received_at,
            )
            disconnect_logged = True
        finally:
            # If the device disconnected mid-capture, drop any partial
            # buffer rather than letting it leak into the next
            # connection's recording slot (mirrors the discard logic in
            # audio_stream.handle_audio_frame for session-mismatched
            # frames).
            #
            # Guard the cleanup by session_id: a stale disconnect must
            # not tear down the active buffer of an unrelated session
            # that may have grabbed the recording slot since (a fresh
            # reconnection or an MCP-driven listen() that took over).
            # The audio_stream layer also tracks the recording session,
            # so we double-check via is_recording_session().
            if self._device_driven_session_id == session_id and (
                is_recording_session(session_id)
            ):
                self._device_driven_session_id = None
                discarded = stop_recording()
                if discarded:
                    logger.warning(
                        "device-driven listen aborted mid-capture: "
                        "session=%s discarded %d frames",
                        session_id, len(discarded),
                    )
            elif self._device_driven_session_id == session_id:
                # Our handler thought it owned the slot, but audio_stream
                # disagrees — clear our local flag without tearing down
                # the slot, then keep going.
                self._device_driven_session_id = None
            connection.disconnect()
            async with self._lock:
                self._unregister_connection(connection, device_id)

    async def _init_device(self, connection: ESP32Connection, device_id: str) -> None:
        """Initialize MCP session with a newly connected device."""
        if await connection.initialize(
            vision_url=self._vision_url,
            vision_token=self._vision_token,
        ):
            await connection.discover_tools()
            if not connection.tools_discovered:
                logger.error("ESP32 tools discovery failed")
                return
            await self._auto_render_idle_avatar(connection, device_id)
            logger.info(
                "ESP32 ready: device=%s tools=%d",
                device_id,
                len(connection.tools),
            )
        else:
            logger.error("ESP32 MCP initialization failed")

    async def _auto_render_idle_avatar(
        self, connection: ESP32Connection, device_id: str
    ) -> None:
        """Best-effort idle avatar render after a fresh device session init."""
        if connection.avatar_render_sent:
            return

        logger.info(
            "auto-rendering idle avatar (no explicit set_avatar yet): device=%s",
            device_id,
        )
        try:
            _result, error = await connection.call_tool(
                _SET_AVATAR_TOOL,
                {"face": "idle"},
            )
        except Exception as exc:
            logger.warning(
                "auto-rendering idle avatar failed: device=%s error=%s",
                device_id,
                exc,
            )
            return

        if error:
            logger.warning(
                "auto-rendering idle avatar failed: device=%s error=%s",
                device_id,
                error,
            )

    async def _emit_stackchan_event(self, payload: dict[str, Any]) -> None:
        """Forward a firmware-originated stackchan event to the MCP client."""
        event_type = payload.get("event_type")
        subtype = payload.get("subtype")
        duration_ms = payload.get("duration_ms")
        ts = payload.get("ts")
        session_id = payload.get("session_id")

        if event_type != "touch":
            logger.warning("Malformed stackchan-event frame: event_type=%r", event_type)
            return
        if subtype not in {"tap", "stroke"}:
            logger.warning("Malformed stackchan-event frame: subtype=%r", subtype)
            return
        if (
            isinstance(duration_ms, bool)
            or not isinstance(duration_ms, int)
            or duration_ms < 0
        ):
            logger.warning(
                "Malformed stackchan-event frame: duration_ms=%r",
                duration_ms,
            )
            return
        if isinstance(ts, bool) or not isinstance(ts, int) or ts < 0:
            logger.warning("Malformed stackchan-event frame: ts=%r", ts)
            return
        if not isinstance(session_id, str) or not session_id:
            logger.warning("Malformed stackchan-event frame: session_id=%r", session_id)
            return

        config = self._notify_config
        message = config.messages.get(
            (event_type, subtype),
            DEFAULT_MESSAGE_TEMPLATES[(event_type, subtype)],
        )
        ts_unix = time.time()
        event_payload = {
            "event_type": event_type,
            "subtype": subtype,
            "duration_ms": duration_ms,
            "action": message.action,
            "ts": ts,
            "ts_unix": ts_unix,
            "session_id": session_id,
        }
        legacy_params = {
            "event_type": event_type,
            "subtype": subtype,
            "duration_ms": duration_ms,
            "action": message.action,
            "ts": ts,
            "session_id": session_id,
        }
        logger.info(
            "stackchan-event: %s/%s action=%s duration=%sms ts=%s session=%s",
            event_type,
            subtype,
            message.action,
            duration_ms,
            ts,
            session_id,
        )

        if not (
            config.legacy_event_enabled
            or config.channels_enabled
            or config.jsonl_enabled
        ):
            logger.info(
                "stackchan-event received and dropped: notification paths disabled"
            )
            return

        from .stdio_server import notify_stackchan_event

        if config.legacy_event_enabled:
            await notify_stackchan_event("stackchan/event", legacy_params)

        if config.channels_enabled:
            content = render_template(message.template, event_payload)
            # Channel notification meta must be all-string per CC binary's
            # Zod schema (matches public plugins: telegram/discord/imessage
            # all use string fields like chat_id, message_id, ts in ISO).
            channel_meta = {
                "event_type": event_type,
                "subtype": subtype,
                "duration_ms": str(duration_ms),
                "action": message.action,
                "ts": str(ts),
                "ts_unix": str(ts_unix),
                "session_id": session_id,
            }
            await notify_stackchan_event(
                "notifications/claude/channel",
                {"content": content, "meta": channel_meta},
            )

        if config.jsonl_enabled:
            # ``log_event`` swallows OS / permission errors internally; the
            # broad except below is a second-tier guard so any unforeseen
            # helper bug cannot break the in-band notification paths above.
            from .event_log import log_event

            try:
                log_event(
                    event_type=event_type,
                    subtype=subtype,
                    duration_ms=duration_ms,
                    ts=ts,
                    session_id=session_id,
                    action=message.action,
                    path=config.jsonl_path,
                    ts_unix=ts_unix,
                )
            except Exception as exc:  # pragma: no cover - defensive guard
                logger.warning(
                    "stackchan-event log persistence raised unexpectedly: %s", exc
                )

    async def call_tool(
        self,
        name: str,
        arguments: dict[str, Any],
        *,
        device_id: str | None = None,
    ) -> ToolCallResult:
        """Call a tool on a connected ESP32 device.

        ``device_id`` selects the target connection. When omitted, the
        default destination is used (first connected device, or the one
        chosen via :meth:`set_default_device`).
        """
        result = await self.call_tools(
            [(name, arguments)], device_id=device_id
        )
        return result[0]

    async def call_tools(
        self,
        calls: Sequence[ToolCall],
        *,
        device_id: str | None = None,
    ) -> list[ToolCallResult]:
        """Call multiple ESP32 tools while preserving per-hardware ordering.

        Existing single-tool callers should continue to use ``call_tool``.
        This helper is for compound gateway flows that can safely overlap
        hardware-independent peripherals, such as servo + LEDs + avatar.
        Calls sharing the same hardware lane are serialized; calls on
        different lanes are dispatched concurrently.

        All calls in one batch target the same device (``device_id`` or
        the default destination).
        """
        if not calls:
            return []
        connection, error = self._resolve_connection(device_id)
        if error is not None:
            return [(None, error) for _ in calls]

        assert connection is not None
        return list(
            await asyncio.gather(
                *(
                    self._call_tool_on_connection(connection, name, arguments)
                    for name, arguments in calls
                )
            )
        )

    async def _call_tool_on_connection(
        self,
        connection: ESP32Connection,
        name: str,
        arguments: dict[str, Any],
    ) -> ToolCallResult:
        lane = _hardware_lane(name)
        lock = self._lane_lock_for(connection, lane)
        async with lock:
            if not connection.connected:
                return None, {"code": -32000, "message": "ESP32 not connected"}
            # Reject if this connection was replaced for its device_id.
            did = getattr(connection, "device_id", None)
            if did is not None and did in self._connections:
                if self._connections[did] is not connection:
                    return None, {
                        "code": -32000,
                        "message": "ESP32 not connected",
                    }
            elif self._connections and connection not in self._connections.values():
                return None, {"code": -32000, "message": "ESP32 not connected"}
            elif (
                not self._connections
                and self._connection is not None
                and connection is not self._connection
            ):
                # Legacy single-connection path (tests inject via
                # ``mgr._connection`` without populating the registry).
                return None, {"code": -32000, "message": "ESP32 not connected"}
            return await connection.call_tool(name, arguments)

    async def send_avatar_set_fetch(
        self,
        url: str,
        token: str,
        mode: str,
        checksum: str,
        expected_size: int,
        timeout: float = 60.0,
        device_id: str | None = None,
    ) -> dict[str, Any]:
        """Forward an avatar_set_fetch to the device and await the reply.

        Phase 4.5 avatar (saiverse-stackchan-addon). Returns a dict with
        keys {ok, checksum, error}; ok=False is returned with a synthetic
        error when no device is connected (rather than raising) so the
        MCP tool surfaces a clean error JSON to the caller. ``device_id``
        selects the target device using phase1's ``_resolve_connection``
        semantics; omitted (None) reproduces the exact pre-phase2
        default-connection behaviour.
        """
        if device_id is None:
            if not self._connection or not self._connection.connected:
                return {"ok": False, "checksum": checksum, "error": "no_device"}
            return await self._connection.send_avatar_set_fetch(
                url, token, mode, checksum, expected_size, timeout
            )
        conn, error = self._resolve_connection(device_id)
        if error is not None:
            return {
                "ok": False,
                "checksum": checksum,
                "error": str(error.get("message", error)),
            }
        assert conn is not None
        return await conn.send_avatar_set_fetch(
            url, token, mode, checksum, expected_size, timeout
        )

    async def send_audio_frame(
        self, opus_frame: bytes, device_id: str | None = None
    ) -> None:
        """Push a single Opus frame to the connected device.

        Used by the TTS pipeline to deliver synthesised audio. Raises
        :class:`ConnectionError` if the target device is not currently
        attached so the orchestrator can surface a clean error to the
        MCP client instead of silently dropping audio. ``device_id``
        selects the target; omitted (None) reproduces the exact
        pre-phase2 default-connection behaviour.
        """
        connection = self._target_connection(device_id)
        await connection.send_audio_frame(opus_frame)

    async def send_tts_state(self, state: str, device_id: str | None = None) -> None:
        """Send a TTS state notification (``start`` / ``stop`` / ...).

        Required around audio frame egress so the device transitions
        into ``kDeviceStateSpeaking`` and back; see
        :meth:`ESP32Connection.send_tts_state` for the full rationale.
        ``device_id`` selects the target; omitted (None) reproduces the
        exact pre-phase2 default-connection behaviour.
        """
        connection = self._target_connection(device_id)
        await connection.send_tts_state(state)

    async def send_listen_state(
        self,
        state: str,
        mode: str = "manual",
        profile: str = "voice",
        device_id: str | None = None,
    ) -> None:
        """Send a listen state notification to put the device into /
        out of listening mode (Issue #91).

        See :meth:`ESP32Connection.send_listen_state` for the wire
        format and the firmware-side dispatch. ``device_id`` selects the
        target; omitted (None) reproduces the exact pre-phase2
        default-connection behaviour.
        """
        connection = self._target_connection(device_id)
        await connection.send_listen_state(state, mode=mode, profile=profile)

    def get_status(self) -> dict[str, Any]:
        """Get current connection status for the default device.

        Multi-device inventory is available via :meth:`list_devices`.
        """
        conn = self._connection
        if conn is None or not conn.connected:
            return {
                "connected": False,
                "device_id": None,
                "tools_count": 0,
            }
        return {
            "connected": True,
            "device_id": conn.device_id,
            # Changes on every WebSocket (re)connection. Lets pollers detect
            # a device reboot even when the reconnect lands between polls and
            # the connected flag never reads false (e.g. a firmware reflash).
            "session_id": conn.session_id,
            "initialized": conn.initialized,
            "tools_count": len(conn.tools),
            "tools": [t.get("name", "") for t in conn.tools],
        }
