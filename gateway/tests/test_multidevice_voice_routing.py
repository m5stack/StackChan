"""Headless multi-device voice-tool routing tests (stdlib unittest only).

Phase2: `say` / `listen` / `load_avatar_set` device_id routing. Mirrors
test_multidevice_routing.py's external-package stub strategy so this runs
under a plain ``python3 -m unittest`` with no gateway extras installed.
No real TTS/STT engine, WebSocket, or opuslib is used anywhere here.
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

    # --- aiohttp (gateway.py / capture_server.py) --------------------------
    # Gated on "aiohttp.web" rather than "aiohttp": test_multidevice_
    # routing.py may already have installed a bare aiohttp stub (no .web
    # submodule) first in the same pytest session, which is insufficient
    # for gateway.py's ``from aiohttp import web``.
    if "aiohttp.web" not in sys.modules:
        aiohttp_mod = types.ModuleType("aiohttp")
        web_mod = types.ModuleType("aiohttp.web")

        class AppKey:
            def __init__(self, name, type_=None):
                self.name = name
                self.type_ = type_

        class Response:
            def __init__(self, *args, **kwargs):
                self.args = args
                self.kwargs = kwargs

        class Request:  # pragma: no cover - type placeholder
            pass

        class Application(dict):
            def __init__(self, *args, **kwargs):
                super().__init__()

        class AppRunner:
            def __init__(self, *args, **kwargs):
                pass

        web_mod.AppKey = AppKey  # type: ignore[attr-defined]
        web_mod.Response = Response  # type: ignore[attr-defined]
        web_mod.Request = Request  # type: ignore[attr-defined]
        web_mod.Application = Application  # type: ignore[attr-defined]
        web_mod.AppRunner = AppRunner  # type: ignore[attr-defined]
        aiohttp_mod.web = web_mod  # type: ignore[attr-defined]

        sys.modules["aiohttp"] = aiohttp_mod
        sys.modules["aiohttp.web"] = web_mod

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

from stackchan_mcp.esp32_client import ESP32Manager  # noqa: E402
from stackchan_mcp import gateway as gateway_module  # noqa: E402
from stackchan_mcp.tts import orchestrator as tts_orchestrator  # noqa: E402
from stackchan_mcp.stt import orchestrator as stt_orchestrator  # noqa: E402
from stackchan_mcp.tts.base import EngineRegistry as TTSRegistry, TTSEngine  # noqa: E402
from stackchan_mcp.stt.base import EngineRegistry as STTRegistry, STTEngine  # noqa: E402


# ---------------------------------------------------------------------------
# Fakes
# ---------------------------------------------------------------------------


class _RecordingConnection:
    """Initialized fake connection that records every call it receives."""

    def __init__(self, device_id: str) -> None:
        self.device_id = device_id
        self.session_id = f"session-{device_id}"
        self.connected = True
        self.initialized = True
        self.protocol_version = 1
        self.tools: list[dict] = []
        self.connected_at: float | None = None
        self.calls: list[tuple[str, dict]] = []
        self.tts_states: list[str] = []
        self.listen_states: list[tuple[str, str]] = []
        self.audio_frames: list[bytes] = []
        self.avatar_fetches: list[dict] = []
        self._tts_lock = asyncio.Lock()
        self._listen_lock = self._tts_lock
        self._tool_lane_locks = {
            name: asyncio.Lock()
            for name in (
                "servo", "wifi", "led", "port_b", "port_c", "avatar",
                "display", "audio", "camera", "touch", "status", "default",
            )
        }

    @property
    def tts_lock(self) -> asyncio.Lock:
        return self._tts_lock

    @property
    def listen_lock(self) -> asyncio.Lock:
        return self._listen_lock

    async def call_tool(self, name: str, arguments: dict) -> tuple[dict, None]:
        self.calls.append((name, arguments))
        payload: Any = {"ok": True}
        if name == "self.robot.get_head_angles":
            payload = {"yaw": 0.0, "pitch": 0.0}
        return (
            {"content": [{"type": "text", "text": __import__("json").dumps(payload)}]},
            None,
        )

    async def send_tts_state(self, state: str) -> None:
        self.tts_states.append(state)

    async def send_audio_frame(self, opus_frame: bytes) -> None:
        self.audio_frames.append(opus_frame)

    async def send_listen_state(self, state: str, mode: str = "manual", profile: str = "voice") -> None:
        self.listen_states.append((state, mode))

    async def send_avatar_set_fetch(self, url, token, mode, checksum, expected_size, timeout=60.0):
        entry = {"url": url, "token": token, "mode": mode, "checksum": checksum}
        self.avatar_fetches.append(entry)
        return {"ok": True, "checksum": checksum}

    def disconnect(self) -> None:
        self.connected = False
        self.initialized = False


def _two_device_manager() -> tuple[ESP32Manager, _RecordingConnection, _RecordingConnection]:
    mgr = ESP32Manager()
    a = _RecordingConnection("dev-a")
    b = _RecordingConnection("dev-b")
    mgr._register_connection(a, a.device_id)  # type: ignore[arg-type]
    mgr._register_connection(b, b.device_id)  # type: ignore[arg-type]
    return mgr, a, b


class _FakeGateway:
    """Duck-typed stand-in exposing only the `.esp32` attribute orchestrators use."""

    def __init__(self, esp32: ESP32Manager) -> None:
        self.esp32 = esp32


def _run(coro):
    return asyncio.run(coro)


def _assert_no_device_traffic(
    testcase: unittest.TestCase, conn: _RecordingConnection, *, label: str
) -> None:
    """Fail unless *conn* received no tool/send traffic (non-target device)."""
    testcase.assertEqual(conn.calls, [], msg=f"{label}.calls")
    testcase.assertEqual(conn.tts_states, [], msg=f"{label}.tts_states")
    testcase.assertEqual(conn.listen_states, [], msg=f"{label}.listen_states")
    testcase.assertEqual(conn.audio_frames, [], msg=f"{label}.audio_frames")
    testcase.assertEqual(conn.avatar_fetches, [], msg=f"{label}.avatar_fetches")


# ---------------------------------------------------------------------------
# ESP32Manager primitives
# ---------------------------------------------------------------------------


class TestManagerVoicePrimitives(unittest.TestCase):
    def test_default_device_id_none_matches_pre_phase2_behavior(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            await mgr.send_tts_state("start")
            await mgr.send_audio_frame(b"frame")
            await mgr.send_listen_state("start", mode="manual")
            self.assertEqual(a.tts_states, ["start"])
            self.assertEqual(a.audio_frames, [b"frame"])
            self.assertEqual(a.listen_states, [("start", "manual")])
            self.assertEqual(b.tts_states, [])
            self.assertEqual(b.audio_frames, [])
            self.assertEqual(b.listen_states, [])

        _run(scenario())

    def test_explicit_device_id_routes_only_to_that_device(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            await mgr.send_tts_state("start", device_id="dev-b")
            await mgr.send_audio_frame(b"frame-b", device_id="dev-b")
            await mgr.send_listen_state("start", device_id="dev-b")
            self.assertEqual(b.tts_states, ["start"])
            self.assertEqual(b.audio_frames, [b"frame-b"])
            self.assertEqual(a.tts_states, [])
            self.assertEqual(a.audio_frames, [])

        _run(scenario())

    def test_unknown_device_id_raises_connection_error_not_crash(self) -> None:
        async def scenario() -> None:
            mgr, _a, _b = _two_device_manager()
            for coro_factory in (
                lambda: mgr.send_tts_state("start", device_id="no-such"),
                lambda: mgr.send_audio_frame(b"x", device_id="no-such"),
                lambda: mgr.send_listen_state("start", device_id="no-such"),
            ):
                with self.assertRaises(ConnectionError):
                    await coro_factory()

        _run(scenario())

    def test_send_avatar_set_fetch_routes_by_device_id_and_soft_errors(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            result = await mgr.send_avatar_set_fetch(
                "http://x/avatar_set/1", "tok", "layered", "sha", 100, device_id="dev-b"
            )
            self.assertTrue(result["ok"])
            self.assertEqual(len(b.avatar_fetches), 1)
            self.assertEqual(a.avatar_fetches, [])

            bad = await mgr.send_avatar_set_fetch(
                "http://x/avatar_set/1", "tok", "layered", "sha", 100, device_id="no-such"
            )
            self.assertFalse(bad["ok"])

        _run(scenario())

    def test_connection_for_and_locks_are_distinct_per_device(self) -> None:
        mgr, a, b = _two_device_manager()
        self.assertIs(mgr.connection_for("dev-a"), a)
        self.assertIs(mgr.connection_for("dev-b"), b)
        self.assertIsNone(mgr.connection_for("no-such"))
        self.assertIs(mgr.connection_for(None), mgr.connection)

        self.assertIsNot(mgr.tts_lock_for("dev-a"), mgr.tts_lock_for("dev-b"))
        self.assertIsNot(mgr.listen_lock_for("dev-a"), mgr.listen_lock_for("dev-b"))
        # Same device consistently returns the same lock object.
        self.assertIs(mgr.tts_lock_for("dev-a"), mgr.tts_lock_for("dev-a"))
        self.assertIs(mgr.tts_lock_for("dev-a"), a.tts_lock)


# ---------------------------------------------------------------------------
# Orchestrator-level per-device routing helpers
# ---------------------------------------------------------------------------


class TestTtsOrchestratorHelpers(unittest.TestCase):
    def test_helpers_route_by_device_id_and_default_matches_no_arg_call(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            await tts_orchestrator._call_tool_for_device(
                mgr, "self.display.set_avatar", {"face": "idle"}, "dev-b"
            )
            self.assertEqual(len(b.calls), 1)
            self.assertEqual(a.calls, [])

            await tts_orchestrator._call_tool_for_device(
                mgr, "self.display.set_avatar", {"face": "idle"}, None
            )
            self.assertEqual(len(a.calls), 1)

            await tts_orchestrator._send_tts_state_for_device(mgr, "start", "dev-b")
            self.assertEqual(b.tts_states, ["start"])
            self.assertEqual(a.tts_states, [])

            await tts_orchestrator._send_audio_frame_for_device(mgr, b"f", "dev-b")
            self.assertEqual(b.audio_frames, [b"f"])
            self.assertEqual(a.audio_frames, [])

            self.assertIs(
                tts_orchestrator._tts_lock_for_device(mgr, "dev-a"),
                a.tts_lock,
            )
            self.assertIsNot(
                tts_orchestrator._tts_lock_for_device(mgr, "dev-a"),
                tts_orchestrator._tts_lock_for_device(mgr, "dev-b"),
            )

        _run(scenario())


class TestSttOrchestratorHelpers(unittest.TestCase):
    def test_helpers_route_by_device_id_and_default_matches_no_arg_call(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            await stt_orchestrator._call_tool_for_device(
                mgr, "self.robot.get_head_angles", {}, "dev-b"
            )
            self.assertEqual(len(b.calls), 1)
            self.assertEqual(a.calls, [])

            await stt_orchestrator._send_listen_state_for_device(
                mgr, "start", "dev-b", mode="manual"
            )
            self.assertEqual(b.listen_states, [("start", "manual")])
            self.assertEqual(a.listen_states, [])

            self.assertIs(
                stt_orchestrator._listen_lock_for_device(mgr, "dev-a"),
                a.listen_lock,
            )
            self.assertIsNot(
                stt_orchestrator._listen_lock_for_device(mgr, "dev-a"),
                stt_orchestrator._listen_lock_for_device(mgr, "dev-b"),
            )

        _run(scenario())


# ---------------------------------------------------------------------------
# synthesize_and_send / listen_and_transcribe: full-path device consistency
# ---------------------------------------------------------------------------


class _FakeTTSEngine(TTSEngine):
    name = "faketts"
    supports_emoji_style = False

    async def synthesize(self, text: str, **opts: Any) -> bytes:
        return b"\x00\x01" * 800


class _FakeSTTEngine(STTEngine):
    name = "fakestt"

    async def transcribe(self, pcm: bytes, **opts: Any) -> dict:
        return {"text": "", "language": "ja"}


class TestSynthesizeAndSendDeviceRouting(unittest.TestCase):
    def test_say_pipeline_targets_only_the_explicit_device(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gateway = _FakeGateway(mgr)
            registry = TTSRegistry()
            registry.register(_FakeTTSEngine())

            original_encode = tts_orchestrator.encode_opus_frames
            tts_orchestrator.encode_opus_frames = lambda pcm: [b"op1", b"op2"]
            try:
                result = await tts_orchestrator.synthesize_and_send(
                    {"text": "hello there", "voice": "faketts"},
                    gateway=gateway,
                    registry=registry,
                    device_id="dev-b",
                )
            finally:
                tts_orchestrator.encode_opus_frames = original_encode

            self.assertTrue(result["spoke"])
            self.assertEqual(b.tts_states, ["start", "stop"])
            self.assertEqual(b.audio_frames, [b"op1", b"op2"])
            _assert_no_device_traffic(self, a, label="non-target a")

        _run(scenario())

    def test_say_pipeline_omitted_device_id_matches_default_device(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gateway = _FakeGateway(mgr)
            registry = TTSRegistry()
            registry.register(_FakeTTSEngine())

            original_encode = tts_orchestrator.encode_opus_frames
            tts_orchestrator.encode_opus_frames = lambda pcm: [b"op1"]
            try:
                await tts_orchestrator.synthesize_and_send(
                    {"text": "hi", "voice": "faketts"},
                    gateway=gateway,
                    registry=registry,
                )
            finally:
                tts_orchestrator.encode_opus_frames = original_encode

            self.assertEqual(a.tts_states, ["start", "stop"])
            self.assertEqual(a.audio_frames, [b"op1"])
            _assert_no_device_traffic(self, b, label="non-target b")

        _run(scenario())

    def test_say_pipeline_unknown_device_id_raises_runtime_error(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gateway = _FakeGateway(mgr)
            registry = TTSRegistry()
            registry.register(_FakeTTSEngine())
            with self.assertRaises(RuntimeError):
                await tts_orchestrator.synthesize_and_send(
                    {"text": "hi", "voice": "faketts"},
                    gateway=gateway,
                    registry=registry,
                    device_id="no-such-device",
                )
            _assert_no_device_traffic(self, a, label="connected a")
            _assert_no_device_traffic(self, b, label="connected b")

        _run(scenario())


class TestListenAndTranscribeDeviceRouting(unittest.TestCase):
    def test_listen_pipeline_targets_only_the_explicit_device(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gateway = _FakeGateway(mgr)
            registry = STTRegistry()
            registry.register(_FakeSTTEngine())

            original_decode = stt_orchestrator.decode_opus_frames
            stt_orchestrator.decode_opus_frames = lambda frames: b""
            try:
                await stt_orchestrator.listen_and_transcribe(
                    {"duration_ms": 100, "engine": "fakestt", "motion": "none"},
                    gateway=gateway,
                    registry=registry,
                    device_id="dev-b",
                )
            finally:
                stt_orchestrator.decode_opus_frames = original_decode

            self.assertEqual(b.listen_states, [("start", "manual"), ("stop", "manual")])
            _assert_no_device_traffic(self, a, label="non-target a")

        _run(scenario())

    def test_listen_pipeline_unknown_device_id_raises_runtime_error(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gateway = _FakeGateway(mgr)
            registry = STTRegistry()
            registry.register(_FakeSTTEngine())
            with self.assertRaises(RuntimeError):
                await stt_orchestrator.listen_and_transcribe(
                    {"duration_ms": 100, "engine": "fakestt", "motion": "none"},
                    gateway=gateway,
                    registry=registry,
                    device_id="no-such-device",
                )
            _assert_no_device_traffic(self, a, label="connected a")
            _assert_no_device_traffic(self, b, label="connected b")

        _run(scenario())

    def test_listen_pipeline_face_motion_targets_only_the_explicit_device(self) -> None:
        """face-only motion must not restore the idle face on the default unit."""

        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gateway = _FakeGateway(mgr)
            registry = STTRegistry()
            registry.register(_FakeSTTEngine())

            original_decode = stt_orchestrator.decode_opus_frames
            stt_orchestrator.decode_opus_frames = lambda frames: b""
            try:
                await stt_orchestrator.listen_and_transcribe(
                    {"duration_ms": 100, "engine": "fakestt", "motion": "face-only"},
                    gateway=gateway,
                    registry=registry,
                    device_id="dev-b",
                )
            finally:
                stt_orchestrator.decode_opus_frames = original_decode

            self.assertEqual(b.listen_states, [("start", "manual"), ("stop", "manual")])
            self.assertEqual(
                [name for name, _args in b.calls],
                ["self.display.set_avatar", "self.display.set_avatar"],
            )
            self.assertEqual(b.calls[0][1], {"face": "thinking"})
            self.assertEqual(b.calls[1][1], {"face": "idle"})
            _assert_no_device_traffic(self, a, label="non-target a")

        _run(scenario())


# ---------------------------------------------------------------------------
# Gateway.load_avatar_set -> ESP32Manager.send_avatar_set_fetch forwarding
# ---------------------------------------------------------------------------


class TestGatewayLoadAvatarSetDeviceRouting(unittest.TestCase):
    def test_load_avatar_set_forwards_device_id(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gw = object.__new__(gateway_module.Gateway)
            gw.esp32 = mgr
            gw._capture_app = object()  # truthy sentinel; bypasses the aiohttp app gate

            async def fake_stage_avatar_set(app, mode, payload):
                return "shortid", "token123", "deadbeef"

            original_stage = gateway_module.stage_avatar_set
            original_exists = gateway_module.os.path.exists
            gateway_module.stage_avatar_set = fake_stage_avatar_set
            gateway_module.os.path.exists = lambda p: True
            try:
                import builtins

                original_open = builtins.open

                class _FakeFile:
                    def __enter__(self):
                        return self

                    def __exit__(self, *exc):
                        return False

                    def read(self):
                        return b"\x00" * (14 * 160 * 120 * 2)

                builtins.open = lambda *a, **k: _FakeFile()
                try:
                    result = await gateway_module.Gateway.load_avatar_set(
                        gw, "/fake/path", "layered", device_id="dev-b"
                    )
                finally:
                    builtins.open = original_open
            finally:
                gateway_module.stage_avatar_set = original_stage
                gateway_module.os.path.exists = original_exists

            self.assertTrue(result["ok"])
            self.assertEqual(len(b.avatar_fetches), 1)
            self.assertEqual(a.avatar_fetches, [])

        _run(scenario())


# ---------------------------------------------------------------------------
# stdio_server dispatch wiring (source-level): stdio_server.py itself pulls
# in anyio + the mcp SDK, neither of which is installed in this headless
# unittest environment (mirrors why test_stdio_server.py isn't runnable
# here either), so behavioral dispatch is verified by asserting each of the
# 3 phase2 call sites forwards the already-popped device_id, rather than by
# importing and exercising the module directly.
# ---------------------------------------------------------------------------


class TestStdioDispatchSourceWiring(unittest.TestCase):
    def test_say_listen_load_avatar_set_all_forward_device_id(self) -> None:
        src = (_GATEWAY_ROOT / "stackchan_mcp" / "stdio_server.py").read_text()
        self.assertNotIn("TODO(phase2): per-device routing for voice tools", src)

        say_block = src[src.index('if name == "say"'):src.index('if name == "listen"')]
        self.assertIn("synthesize_and_send(", say_block)
        self.assertIn("device_id=device_id", say_block)

        listen_block = src[src.index('if name == "listen"'):src.index('if name == "load_avatar_set"')]
        self.assertIn("listen_and_transcribe(", listen_block)
        self.assertIn("device_id=device_id", listen_block)

        load_avatar_block = src[
            src.index('if name == "load_avatar_set"'):
            src.index('if name == "stackchan_follow_pose_stream"')
        ]
        self.assertIn("gateway.load_avatar_set(", load_avatar_block)
        self.assertIn("device_id=device_id", load_avatar_block)


class TestGatewayWideCaptureLock(unittest.TestCase):
    def test_listen_on_second_device_waits_for_the_first_capture(self) -> None:
        async def scenario() -> None:
            mgr, a, b = _two_device_manager()
            gateway = _FakeGateway(mgr)
            registry = STTRegistry()
            registry.register(_FakeSTTEngine())
            events: list[tuple[str, str]] = []

            for conn in (a, b):
                async def send(state, mode="manual", profile="voice", _c=conn):
                    events.append((_c.device_id, state))

                conn.send_listen_state = send  # type: ignore[method-assign]

            original_decode = stt_orchestrator.decode_opus_frames
            stt_orchestrator.decode_opus_frames = lambda frames: b""
            try:
                await asyncio.gather(
                    *(
                        stt_orchestrator.listen_and_transcribe(
                            {"duration_ms": 100, "engine": "fakestt", "motion": "none"},
                            gateway=gateway,
                            registry=registry,
                            device_id=dev,
                        )
                        for dev in ("dev-a", "dev-b")
                    )
                )
            finally:
                stt_orchestrator.decode_opus_frames = original_decode

            self.assertEqual(len(events), 4)
            first = events[0][0]
            self.assertEqual(events[1], (first, "stop"))
            second = events[2][0]
            self.assertNotEqual(first, second)
            self.assertEqual(events[2:], [(second, "start"), (second, "stop")])

        _run(scenario())


if __name__ == "__main__":
    unittest.main()
