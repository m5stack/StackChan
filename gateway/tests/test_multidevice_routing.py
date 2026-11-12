"""Headless multi-device routing tests (stdlib unittest only).

External packages (websockets, pydantic, aiohttp, ...) are stubbed into
sys.modules before importing stackchan_mcp so this module runs under a
plain ``python3 -m unittest`` with no gateway extras installed.
"""

from __future__ import annotations

import asyncio
import sys
import types
import unittest
from pathlib import Path
from typing import Any


# ---------------------------------------------------------------------------
# Import path + external-module stubs (must run before stackchan_mcp imports)
# ---------------------------------------------------------------------------

_GATEWAY_ROOT = Path(__file__).resolve().parents[1]
if str(_GATEWAY_ROOT) not in sys.path:
    sys.path.insert(0, str(_GATEWAY_ROOT))


def _install_external_stubs() -> None:
    """Inject minimal stand-ins for optional third-party packages."""

    # --- websockets -------------------------------------------------------
    if "websockets" not in sys.modules:
        ws_mod = types.ModuleType("websockets")
        ws_mod.serve = lambda *a, **k: None  # type: ignore[attr-defined]
        ws_exc = types.ModuleType("websockets.exceptions")

        class ConnectionClosed(Exception):
            def __init__(self, rcvd=None, sent=None):
                super().__init__("closed")
                self.rcvd = rcvd
                self.sent = sent

        ws_exc.ConnectionClosed = ConnectionClosed  # type: ignore[attr-defined]
        ws_mod.exceptions = ws_exc  # type: ignore[attr-defined]

        ws_datastructures = types.ModuleType("websockets.datastructures")

        class Headers(dict):
            pass

        ws_datastructures.Headers = Headers  # type: ignore[attr-defined]
        ws_mod.datastructures = ws_datastructures  # type: ignore[attr-defined]

        ws_http11 = types.ModuleType("websockets.http11")

        class Request:  # pragma: no cover - type placeholder
            pass

        class Response:
            def __init__(self, *args, **kwargs):
                self.args = args
                self.kwargs = kwargs

        ws_http11.Request = Request  # type: ignore[attr-defined]
        ws_http11.Response = Response  # type: ignore[attr-defined]
        ws_mod.http11 = ws_http11  # type: ignore[attr-defined]

        ws_asyncio = types.ModuleType("websockets.asyncio")
        ws_asyncio_server = types.ModuleType("websockets.asyncio.server")
        ws_asyncio_server.ServerConnection = object  # type: ignore[attr-defined]
        ws_asyncio.server = ws_asyncio_server  # type: ignore[attr-defined]

        sys.modules["websockets"] = ws_mod
        sys.modules["websockets.exceptions"] = ws_exc
        sys.modules["websockets.datastructures"] = ws_datastructures
        sys.modules["websockets.http11"] = ws_http11
        sys.modules["websockets.asyncio"] = ws_asyncio
        sys.modules["websockets.asyncio.server"] = ws_asyncio_server

    # --- pydantic (protocol.HelloResponse) --------------------------------
    if "pydantic" not in sys.modules:
        pydantic = types.ModuleType("pydantic")

        class _BaseModel:
            def __init__(self, **kwargs):
                for key, value in kwargs.items():
                    setattr(self, key, value)

            def model_dump_json(self) -> str:
                import json

                return json.dumps(self.__dict__)

            def model_dump(self) -> dict:
                return dict(self.__dict__)

        def Field(default=None, **kwargs):  # noqa: N802 - pydantic API
            return default

        pydantic.BaseModel = _BaseModel  # type: ignore[attr-defined]
        pydantic.Field = Field  # type: ignore[attr-defined]
        sys.modules["pydantic"] = pydantic

    # --- aiohttp (audio_input_hook) ---------------------------------------
    # Gated on "aiohttp.web" rather than "aiohttp": test_multidevice_voice_
    # routing.py needs the fuller aiohttp.web stub (gateway.py imports it)
    # and must still install it even when this bare stub already ran first
    # in the same pytest session (collection order is not guaranteed).
    if "aiohttp.web" not in sys.modules:
        aiohttp = types.ModuleType("aiohttp")
        sys.modules["aiohttp"] = aiohttp

    # --- yaml (notify_config) if missing ----------------------------------
    if "yaml" not in sys.modules:
        try:
            import yaml  # noqa: F401
        except ImportError:
            yaml_mod = types.ModuleType("yaml")

            def safe_load(stream):  # pragma: no cover
                return None

            yaml_mod.safe_load = safe_load  # type: ignore[attr-defined]
            sys.modules["yaml"] = yaml_mod


_install_external_stubs()

from stackchan_mcp.esp32_client import ESP32Connection, ESP32Manager  # noqa: E402


# ---------------------------------------------------------------------------
# Fakes
# ---------------------------------------------------------------------------


class _FakeWebSocket:
    """Minimal WebSocket stand-in for ESP32Connection construction."""

    def __init__(self) -> None:
        self.sent: list[Any] = []

    async def send(self, data: Any) -> None:
        self.sent.append(data)


class _RecordingConnection:
    """Initialized fake connection that records call_tool invocations."""

    def __init__(self, device_id: str) -> None:
        self.device_id = device_id
        self.session_id = f"session-{device_id}"
        self.connected = True
        self.initialized = True
        self.tools: list[dict] = []
        self.connected_at: float | None = None
        self.calls: list[tuple[str, dict]] = []
        self._tts_lock = asyncio.Lock()
        self._listen_lock = self._tts_lock
        self._tool_lane_locks = {
            "servo": asyncio.Lock(),
            "wifi": asyncio.Lock(),
            "led": asyncio.Lock(),
            "port_b": asyncio.Lock(),
            "port_c": asyncio.Lock(),
            "avatar": asyncio.Lock(),
            "display": asyncio.Lock(),
            "audio": asyncio.Lock(),
            "camera": asyncio.Lock(),
            "touch": asyncio.Lock(),
            "status": asyncio.Lock(),
            "default": asyncio.Lock(),
        }

    @property
    def tts_lock(self) -> asyncio.Lock:
        return self._tts_lock

    @property
    def listen_lock(self) -> asyncio.Lock:
        return self._listen_lock

    async def call_tool(self, name: str, arguments: dict) -> tuple[dict, None]:
        self.calls.append((name, arguments))
        return (
            {
                "content": [
                    {
                        "type": "text",
                        "text": f"ok:{self.device_id}:{name}",
                    }
                ]
            },
            None,
        )

    def disconnect(self) -> None:
        self.connected = False
        self.initialized = False


def _run(coro):
    return asyncio.run(coro)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class TestMultiDeviceRouting(unittest.TestCase):
    """Phase-1 multi-device connection registry and tool routing."""

    def test_two_connections_held_without_kick(self) -> None:
        """① Two distinct device_ids coexist; neither is kicked."""
        mgr = ESP32Manager()
        a = _RecordingConnection("44:1b:f6:e4:7a:48")
        b = _RecordingConnection("68:ee:8f:d7:40:5c")

        mgr._register_connection(a, a.device_id)  # type: ignore[arg-type]
        mgr._register_connection(b, b.device_id)  # type: ignore[arg-type]

        self.assertTrue(a.connected)
        self.assertTrue(b.connected)
        self.assertEqual(set(mgr._connections), {a.device_id, b.device_id})
        devices = mgr.list_devices()
        self.assertEqual(len(devices), 2)
        ids = {d["device_id"] for d in devices}
        self.assertEqual(ids, {a.device_id, b.device_id})
        # First connected remains default.
        self.assertEqual(mgr.default_device_id, a.device_id)

    def test_device_id_routes_to_correct_connection(self) -> None:
        """② Explicit device_id delivers the tool call to that connection."""

        async def scenario() -> None:
            mgr = ESP32Manager()
            a = _RecordingConnection("dev-a")
            b = _RecordingConnection("dev-b")
            mgr._register_connection(a, a.device_id)  # type: ignore[arg-type]
            mgr._register_connection(b, b.device_id)  # type: ignore[arg-type]

            result, error = await mgr.call_tool(
                "self.led.set_color",
                {"r": 1, "g": 2, "b": 3},
                device_id="dev-b",
            )
            self.assertIsNone(error)
            self.assertEqual(result["content"][0]["text"], "ok:dev-b:self.led.set_color")
            self.assertEqual(a.calls, [])
            self.assertEqual(len(b.calls), 1)
            self.assertEqual(b.calls[0][0], "self.led.set_color")

        _run(scenario())

    def test_omitted_device_id_uses_default(self) -> None:
        """③ Omitting device_id sends to the default (first) device."""

        async def scenario() -> None:
            mgr = ESP32Manager()
            a = _RecordingConnection("dev-a")
            b = _RecordingConnection("dev-b")
            mgr._register_connection(a, a.device_id)  # type: ignore[arg-type]
            mgr._register_connection(b, b.device_id)  # type: ignore[arg-type]

            result, error = await mgr.call_tool(
                "self.robot.set_head_angles",
                {"yaw": 0, "pitch": 45},
            )
            self.assertIsNone(error)
            self.assertEqual(
                result["content"][0]["text"],
                "ok:dev-a:self.robot.set_head_angles",
            )
            self.assertEqual(len(a.calls), 1)
            self.assertEqual(b.calls, [])

            # set_default_device switches the implicit target.
            switched = mgr.set_default_device("dev-b")
            self.assertTrue(switched["ok"])
            result2, error2 = await mgr.call_tool(
                "self.led.clear",
                {},
            )
            self.assertIsNone(error2)
            self.assertEqual(result2["content"][0]["text"], "ok:dev-b:self.led.clear")
            self.assertEqual(len(b.calls), 1)

        _run(scenario())

    def test_unknown_device_id_returns_error(self) -> None:
        """④ Unknown device_id returns an error dict; does not raise."""

        async def scenario() -> None:
            mgr = ESP32Manager()
            a = _RecordingConnection("dev-a")
            mgr._register_connection(a, a.device_id)  # type: ignore[arg-type]

            result, error = await mgr.call_tool(
                "self.led.clear",
                {},
                device_id="no-such-device",
            )
            self.assertIsNone(result)
            self.assertIsNotNone(error)
            assert error is not None
            self.assertIn("no-such-device", error["message"])
            self.assertEqual(a.calls, [])

            # set_default_device also soft-errors.
            bad = mgr.set_default_device("no-such-device")
            self.assertFalse(bad["ok"])
            self.assertIn("no-such-device", bad["error"])

        _run(scenario())

    def test_same_device_id_reconnect_replaces(self) -> None:
        """⑤ Re-hello with the same device_id replaces the old connection."""
        mgr = ESP32Manager()
        first = _RecordingConnection("dev-same")
        second = _RecordingConnection("dev-same")
        # Distinct session markers so we can tell them apart.
        first.session_id = "session-old"
        second.session_id = "session-new"

        mgr._register_connection(first, "dev-same")  # type: ignore[arg-type]
        self.assertTrue(first.connected)
        self.assertIs(mgr._connections["dev-same"], first)

        mgr._register_connection(second, "dev-same")  # type: ignore[arg-type]
        self.assertFalse(first.connected)
        self.assertTrue(second.connected)
        self.assertIs(mgr._connections["dev-same"], second)
        self.assertEqual(len(mgr._connections), 1)
        self.assertEqual(mgr.default_device_id, "dev-same")
        self.assertIs(mgr.connection, second)

        # A second distinct device is unaffected by the replace.
        other = _RecordingConnection("dev-other")
        mgr._register_connection(other, "dev-other")  # type: ignore[arg-type]
        mgr._register_connection(
            _RecordingConnection("dev-same"), "dev-same"  # type: ignore[arg-type]
        )
        self.assertTrue(other.connected)
        self.assertIn("dev-other", mgr._connections)
        self.assertIn("dev-same", mgr._connections)


class TestRealConnectionLocks(unittest.TestCase):
    """Smoke: real ESP32Connection owns per-device locks."""

    def test_connection_has_per_device_locks(self) -> None:
        conn = ESP32Connection(_FakeWebSocket(), session_id="s1")  # type: ignore[arg-type]
        self.assertIs(conn.tts_lock, conn.listen_lock)
        self.assertIn("servo", conn._tool_lane_locks)
        self.assertIsNot(conn._tool_lane_locks["servo"], conn._tool_lane_locks["led"])


if __name__ == "__main__":
    unittest.main()
