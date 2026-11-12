"""Tests for stdio MCP server tool definitions."""

import json
from pathlib import Path

import pytest
from mcp.types import CallToolRequest, ListToolsRequest

from stackchan_mcp.notify_config import DEFAULT_MESSAGE_TEMPLATES, NotifyConfig
import stackchan_mcp.stdio_server as stdio_server
from stackchan_mcp.stdio_server import (
    CHANNEL_CAPABILITY,
    CHANNEL_NOTIFICATION_METHOD,
    STACKCHAN_CHANNEL_INSTRUCTIONS,
    STACKCHAN_EVENT_INSTRUCTIONS,
    STACKCHAN_EVENT_METHOD,
    STACKCHAN_JSONL_INSTRUCTIONS,
    SPEED_DESCRIPTION,
    _build_experimental_capabilities,
    _build_stackchan_event_instructions,
    _create_initialization_options,
    _resolve_speed_dps,
    create_server,
    notify_stackchan_event,
)
from stackchan_mcp.tts import get_registry


@pytest.fixture(autouse=True)
def _isolate_user_defaults_config(monkeypatch, tmp_path):
    """Keep stdio tests independent from any real user-defaults file."""
    import stackchan_mcp.user_defaults as user_defaults

    config_dir = tmp_path / "user-defaults-config"

    def fake_user_config_path(appname: str, **kwargs):
        assert appname == "stackchan-mcp"
        return config_dir

    monkeypatch.setattr(
        user_defaults.platformdirs,
        "user_config_path",
        fake_user_config_path,
    )
    user_defaults._clear_user_defaults_cache_for_tests()
    stdio_server._reset_ws2812_color_orders_for_tests()
    yield
    user_defaults._clear_user_defaults_cache_for_tests()
    stdio_server._reset_ws2812_color_orders_for_tests()


def _schema_without_device_id(tool):
    schema = dict(tool.inputSchema)
    props = {k: v for k, v in schema.get("properties", {}).items() if k != "device_id"}
    schema["properties"] = props
    return schema


def test_create_server():
    """Server creation succeeds with correct name."""
    server = create_server()
    assert server is not None
    assert server.name == "stackchanmcp"


@pytest.mark.asyncio
async def test_list_tools_includes_get_head_angles():
    """get_head_angles is exposed to MCP clients."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tool_names = [tool.name for tool in result.root.tools]
    assert "get_head_angles" in tool_names


@pytest.mark.asyncio
async def test_get_head_angles_relays_to_esp32(monkeypatch):
    """get_head_angles maps to the ESP32 self.robot.get_head_angles tool."""
    calls = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps({"yaw": 12, "pitch": -3}),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    import stackchan_mcp.stdio_server as stdio_server

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "get_head_angles", "arguments": {}},
        )
    )

    assert calls == [("self.robot.get_head_angles", {})]
    assert json.loads(result.root.content[0].text) == {"yaw": 12, "pitch": -3}


@pytest.mark.asyncio
async def test_list_tools_includes_screen_off_timeout_tools():
    server = create_server()
    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )
    tools = {tool.name: tool for tool in result.root.tools}
    schema = tools["set_off_timeout"].inputSchema
    assert schema["required"] == ["seconds"]
    assert schema["properties"]["seconds"] == {
        "type": "integer", "minimum": 0, "maximum": 86400,
    }
    assert _schema_without_device_id(tools["get_off_timeout"]) == {
        "type": "object", "properties": {},
    }


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("name", "arguments"),
    [
        ("set_off_timeout", {"seconds": 0}),
        ("set_off_timeout", {"seconds": 300}),
        ("set_off_timeout", {"seconds": 86400}),
        ("get_off_timeout", {}),
    ],
)
async def test_screen_off_timeout_relays_to_esp32(monkeypatch, name, arguments):
    calls = []
    payload = {"seconds": arguments.get("seconds", 300), "persistent": True}

    class FakeESP32:
        device_connected = True

        async def call_tool(self, tool_name, tool_arguments, *, device_id=None):
            calls.append((tool_name, tool_arguments))
            return {"content": [{"type": "text", "text": json.dumps(payload)}]}, None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call", params={"name": name, "arguments": arguments},
        )
    )
    assert calls == [(f"self.screen.{name}", arguments)]
    assert json.loads(result.root.content[0].text) == payload


@pytest.mark.asyncio
async def test_list_tools_includes_charge_protection_tools():
    """Charge protection tools expose toggle, query, and threshold schemas."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools = {tool.name: tool for tool in result.root.tools}
    assert "set_charge_protection" in tools
    assert "get_charge_protection" in tools
    assert "set_charge_thresholds" in tools

    set_tool = tools["set_charge_protection"]
    assert _schema_without_device_id(set_tool) == {
        "type": "object",
        "properties": {
            "enabled": {
                "type": "boolean",
                "description": (
                    "True for persistent threshold protection; false to allow "
                    "charging to the PMIC's full-charge termination."
                ),
            },
        },
        "required": ["enabled"],
    }
    assert "opt-in" in set_tool.description
    assert "going out" in set_tool.description

    get_tool = tools["get_charge_protection"]
    assert _schema_without_device_id(get_tool) == {"type": "object", "properties": {}}
    assert "persistent and effective" in get_tool.description

    thresholds_tool = tools["set_charge_thresholds"]
    assert _schema_without_device_id(thresholds_tool) == {
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
    }


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("gateway_tool", "request_args", "device_tool", "device_args"),
    [
        (
            "set_charge_protection",
            {"enabled": False},
            "self.power.set_charge_protection",
            {"enabled": False},
        ),
        (
            "get_charge_protection",
            {},
            "self.power.get_charge_protection",
            {},
        ),
        (
            "set_charge_thresholds",
            {"on_below": 35, "off_above": 75},
            "self.power.set_charge_thresholds",
            {"on_below": 35, "off_above": 75},
        ),
    ],
)
async def test_charge_protection_tools_relay_and_preserve_response(
    monkeypatch,
    gateway_tool,
    request_args,
    device_tool,
    device_args,
):
    """Set forwards its boolean, get forwards {}, and response text survives."""
    calls = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(
                            {
                                "ok": True,
                                "charge_protection": False,
                                "persistent": True,
                            }
                        ),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": gateway_tool, "arguments": request_args},
        )
    )

    assert calls == [(device_tool, device_args)]
    assert json.loads(result.root.content[0].text) == {
        "ok": True,
        "charge_protection": False,
        "persistent": True,
    }


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "gateway_tool",
    ["set_charge_protection", "get_charge_protection", "set_charge_thresholds"],
)
async def test_charge_protection_tools_propagate_device_error(
    monkeypatch, gateway_tool
):
    """Firmware/transport errors are returned in the normal gateway format."""

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            return None, {"message": "charge protection unavailable"}

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": gateway_tool,
                "arguments": (
                    {"enabled": False}
                    if gateway_tool == "set_charge_protection"
                    else {"on_below": 35, "off_above": 75}
                    if gateway_tool == "set_charge_thresholds"
                    else {}
                ),
            },
        )
    )

    assert json.loads(result.root.content[0].text) == {
        "error": "charge protection unavailable"
    }


@pytest.mark.asyncio
async def test_list_tools_includes_external_power_auto_off_tools():
    """The external-power auto-off toggle exposes set/get schemas."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools = {tool.name: tool for tool in result.root.tools}
    assert _schema_without_device_id(tools["set_external_power_auto_off"]) == {
        "type": "object",
        "properties": {
            "enabled": {
                "type": "boolean",
                "description": (
                    "True to dim and power off on idle even on external "
                    "power; false to stay on while powered."
                ),
            },
        },
        "required": ["enabled"],
    }
    assert _schema_without_device_id(tools["get_external_power_auto_off"]) == {
        "type": "object",
        "properties": {},
    }


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("gateway_tool", "request_args", "device_tool", "device_args"),
    [
        (
            "set_external_power_auto_off",
            {"enabled": True},
            "self.power.set_external_power_auto_off",
            {"enabled": True},
        ),
        (
            "get_external_power_auto_off",
            {},
            "self.power.get_external_power_auto_off",
            {},
        ),
    ],
)
async def test_external_power_auto_off_tools_relay(
    monkeypatch, gateway_tool, request_args, device_tool, device_args
):
    """Set forwards its boolean, get forwards {}, and response text survives."""
    calls = []
    payload = {"ok": True, "external_power_auto_off": True, "persistent": True}

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {"content": [{"type": "text", "text": json.dumps(payload)}]}, None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": gateway_tool, "arguments": request_args},
        )
    )

    assert calls == [(device_tool, device_args)]
    assert json.loads(result.root.content[0].text) == payload


@pytest.mark.asyncio
async def test_list_tools_includes_gateway_config_tools():
    """gateway_config_get/set are exposed with the expected schemas."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools = {tool.name: tool for tool in result.root.tools}
    assert "gateway_config_get" in tools
    assert "gateway_config_set" in tools

    get_schema = _schema_without_device_id(tools["gateway_config_get"])
    assert get_schema == {"type": "object", "properties": {}}
    assert "mDNS" in tools["gateway_config_get"].description
    assert "force_mode" in tools["gateway_config_get"].description

    set_tool = tools["gateway_config_set"]
    set_schema = set_tool.inputSchema
    assert set(set_schema["properties"]) == {
        "url",
        "fallback_url",
        "token",
        "device_id",
    }
    assert "required" not in set_schema
    assert set_schema["properties"]["url"]["type"] == "string"
    assert set_schema["properties"]["fallback_url"]["type"] == "string"
    assert set_schema["properties"]["token"]["type"] == "string"
    assert "empty string clears" in set_tool.description
    assert "next reconnect" in set_tool.description


@pytest.mark.asyncio
async def test_gateway_config_get_relays_to_esp32(monkeypatch):
    """gateway_config_get maps to self.gateway_config.get."""
    calls = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(
                            {
                                "url": "",
                                "fallback_url": "wss://relay.example/",
                                "token_set": True,
                                "force_mode": False,
                                "discovery_enabled": True,
                            }
                        ),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "gateway_config_get", "arguments": {}},
        )
    )

    assert calls == [("self.gateway_config.get", {})]
    assert json.loads(result.root.content[0].text)["discovery_enabled"] is True


@pytest.mark.asyncio
async def test_gateway_config_set_relays_optional_strings(monkeypatch):
    """gateway_config_set forwards provided fields, including empty strings."""
    calls = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(
                            {
                                "ok": True,
                                "updated_keys": ["url", "fallback_url"],
                                "url": "",
                                "fallback_url": "wss://relay.example/",
                                "token_set": False,
                                "discovery_enabled": True,
                            }
                        ),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    arguments = {"url": "", "fallback_url": "wss://relay.example/"}
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "gateway_config_set", "arguments": arguments},
        )
    )

    assert calls == [("self.gateway_config.set", arguments)]
    payload = json.loads(result.root.content[0].text)
    assert payload["ok"] is True
    assert payload["url"] == ""


@pytest.mark.asyncio
async def test_list_tools_includes_touch_sensor_tools():
    """Touch sensor enable/disable tools are exposed with expected schemas."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools = {tool.name: tool for tool in result.root.tools}
    assert "get_touch_sensor_enabled" in tools
    assert "set_touch_sensor_enabled" in tools

    get_tool = tools["get_touch_sensor_enabled"]
    assert _schema_without_device_id(get_tool) == {"type": "object", "properties": {}}
    assert "NVS" in get_tool.description
    assert "local motion response" in get_tool.description
    assert "stackchan/event" in get_tool.description

    set_tool = tools["set_touch_sensor_enabled"]
    assert _schema_without_device_id(set_tool) == {
        "type": "object",
        "properties": {
            "enabled": {
                "type": "boolean",
                "description": (
                    "True to enable tap/stroke detection; false to disable "
                    "local reactions and event emission."
                ),
            },
        },
        "required": ["enabled"],
    }
    assert "persists across reboot" in set_tool.description
    assert "local motion response" in set_tool.description
    assert "stackchan/event" in set_tool.description


@pytest.mark.asyncio
async def test_set_touch_sensor_enabled_relays_to_esp32(monkeypatch):
    """set_touch_sensor_enabled maps to the firmware robot tool."""
    calls = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(
                            {
                                "ok": True,
                                "enabled": arguments["enabled"],
                                "takes_effect": "immediate",
                            }
                        ),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    arguments = {"enabled": False}
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "set_touch_sensor_enabled", "arguments": arguments},
        )
    )

    assert calls == [("self.robot.set_touch_sensor_enabled", arguments)]
    payload = json.loads(result.root.content[0].text)
    assert payload["ok"] is True
    assert payload["enabled"] is False


@pytest.mark.asyncio
async def test_get_touch_sensor_enabled_relays_to_esp32(monkeypatch):
    """get_touch_sensor_enabled maps to the firmware robot tool."""
    calls = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps({"enabled": False}),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "get_touch_sensor_enabled", "arguments": {}},
        )
    )

    assert calls == [("self.robot.get_touch_sensor_enabled", {})]
    assert json.loads(result.root.content[0].text) == {"enabled": False}


@pytest.mark.asyncio
async def test_list_tools_includes_set_mouth_sequence():
    """set_mouth_sequence is exposed to MCP clients with an array schema."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tool = next((t for t in result.root.tools if t.name == "set_mouth_sequence"), None)
    assert tool is not None, "set_mouth_sequence tool should be registered"

    schema = tool.inputSchema
    assert schema["properties"]["steps"]["type"] == "array"
    assert schema["properties"]["steps"]["minItems"] == 1
    assert schema["properties"]["steps"]["maxItems"] == 256

    item_schema = schema["properties"]["steps"]["items"]
    assert item_schema["properties"]["shape"]["enum"] == [
        "closed",
        "half",
        "open",
        "e",
        "u",
    ]
    assert item_schema["properties"]["duration_ms"]["minimum"] == 10
    assert item_schema["properties"]["duration_ms"]["maximum"] == 10000
    assert set(item_schema["required"]) == {"shape", "duration_ms"}


@pytest.mark.asyncio
async def test_list_tools_includes_say():
    """say is exposed to MCP clients with text required."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tool = next((t for t in result.root.tools if t.name == "say"), None)
    assert tool is not None, "say tool should be registered"

    schema = tool.inputSchema
    assert schema["properties"]["text"]["type"] == "string"
    assert "voice" in schema["properties"]
    assert "speaker_id" in schema["properties"]
    assert "reference_audio" in schema["properties"]
    assert schema["required"] == ["text"]


@pytest.mark.asyncio
async def test_say_unknown_voice_returns_clean_error():
    """say with an unknown voice returns a clean error JSON, not a traceback.

    This invariant holds regardless of which engines happen to be
    registered at the default level — the orchestrator must surface a
    NotImplementedError to the caller as MCP error JSON.
    """
    server = create_server()
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": "say",
                "arguments": {"text": "hello", "voice": "nonexistent_engine"},
            },
        )
    )

    payload = json.loads(result.root.content[0].text)
    assert "error" in payload
    assert "nonexistent_engine" in payload["error"]


@pytest.mark.asyncio
async def test_say_rejects_empty_text_with_clean_error():
    """say with empty text returns a clean ValueError-shaped error."""
    server = create_server()
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "say", "arguments": {"text": ""}},
        )
    )

    payload = json.loads(result.root.content[0].text)
    assert "error" in payload
    assert "text" in payload["error"]


@pytest.mark.asyncio
async def test_say_returns_clean_error_when_device_disconnected(monkeypatch):
    """say without a connected ESP32 surfaces a clean MCP error JSON.

    Validation passes (text + registered engine), the orchestrator
    needs to push frames somewhere, and the device gate fires. The
    handler must turn that into ``{"error": "..."}`` rather than
    leaking a stack trace through the MCP transport.
    """

    class FakeESP32:
        device_connected = False

        def get_status(self):
            return {"connected": False}

    class FakeGateway:
        esp32 = FakeESP32()

    import stackchan_mcp.stdio_server as stdio_server

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "say", "arguments": {"text": "hello"}},
        )
    )

    payload = json.loads(result.root.content[0].text)
    assert "error" in payload
    msg = payload["error"].lower()
    assert "esp32" in msg or "device" in msg


@pytest.mark.asyncio
async def test_default_registry_includes_voicevox():
    """The default registry registers VOICEVOX at import time.

    PR2 of Issue #70 wires VOICEVOX in via ``tts/__init__.py`` so users
    who install the ``[tts]`` extra can call ``say`` without needing
    to register an engine themselves. This test pins that contract.
    """
    assert "voicevox" in get_registry().names()


# ---------------------------------------------------------------------------
# say handler regression tests — degraded VOICEVOX / mid-stream disconnect
# must produce error JSON, not stack traces. Codex adversarial review
# flagged that this contract was previously only verified at the
# orchestrator level; these tests close the loop through create_server().
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_say_returns_error_json_when_voicevox_returns_5xx(monkeypatch):
    """A 503 from VOICEVOX surfaces as ``{"error": ...}``, not a traceback."""
    httpx = pytest.importorskip("httpx")

    from stackchan_mcp.tts import EngineRegistry, TTSEngine
    import stackchan_mcp.tts.orchestrator as orchestrator
    import stackchan_mcp.stdio_server as stdio_server

    class _HttpFailEngine(TTSEngine):
        name = "voicevox"

        async def synthesize(self, text, **opts):
            request = httpx.Request("POST", "http://test/audio_query")
            response = httpx.Response(503, request=request, text="overloaded")
            raise httpx.HTTPStatusError(
                "503", request=request, response=response
            )

    reg = EngineRegistry()
    reg.register(_HttpFailEngine())

    class FakeESP32:
        device_connected = True

        def get_status(self):
            return {"connected": True}

        async def send_audio_frame(self, frame):
            raise AssertionError("synthesise should have failed before push")

        async def send_tts_state(self, state):  # noqa: ARG002 - test stub
            return None

    class FakeGateway:
        esp32 = FakeESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(orchestrator, "get_registry", lambda: reg)

    server = create_server()
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "say", "arguments": {"text": "hello"}},
        )
    )
    payload = json.loads(result.root.content[0].text)
    assert "error" in payload
    assert "voicevox" in payload["error"].lower()


@pytest.mark.asyncio
async def test_say_returns_error_json_when_device_disconnects_mid_stream(
    monkeypatch,
):
    """A mid-stream disconnect surfaces as ``{"error": ...}``."""
    from stackchan_mcp.tts import EngineRegistry, TTSEngine
    import stackchan_mcp.tts.orchestrator as orchestrator
    import stackchan_mcp.stdio_server as stdio_server

    pcm = b"\x01\x00" * 1440  # ~ 1.5 frames

    class _PCMEngine(TTSEngine):
        name = "voicevox"

        async def synthesize(self, text, **opts):
            return pcm

    reg = EngineRegistry()
    reg.register(_PCMEngine())

    def fake_encode(_pcm, **kwargs):
        return iter([b"opus_a", b"opus_b"])

    class FailingESP32:
        device_connected = True

        def __init__(self):
            self.frames: list[bytes] = []

        def get_status(self):
            return {"connected": True}

        async def send_audio_frame(self, frame):
            if self.frames:
                raise ConnectionError("simulated disconnect")
            self.frames.append(frame)

        async def send_tts_state(self, state):  # noqa: ARG002 - test stub
            return None

    class FakeGateway:
        esp32 = FailingESP32()

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(orchestrator, "get_registry", lambda: reg)
    monkeypatch.setattr(orchestrator, "encode_opus_frames", fake_encode)

    server = create_server()
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "say", "arguments": {"text": "hello"}},
        )
    )
    payload = json.loads(result.root.content[0].text)
    assert "error" in payload
    msg = payload["error"].lower()
    assert "disconnect" in msg or "frame" in msg


@pytest.mark.asyncio
async def test_set_mouth_sequence_relays_steps_as_json_string(monkeypatch):
    """set_mouth_sequence serialises steps to JSON for the firmware.

    The ESP32 MCP Property type system only supports string/integer/boolean,
    so the gateway flattens the steps array to a JSON string under
    `steps_json` before forwarding to self.display.set_mouth_sequence.
    """
    calls = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, name, arguments, *, device_id=None):
            calls.append((name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(
                            {"ok": True, "queued_steps": 2, "estimated_duration_ms": 160}
                        ),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    import stackchan_mcp.stdio_server as stdio_server

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    steps = [
        {"shape": "open", "duration_ms": 80},
        {"shape": "closed", "duration_ms": 80},
    ]
    await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "set_mouth_sequence", "arguments": {"steps": steps}},
        )
    )

    assert len(calls) == 1
    name, arguments = calls[0]
    assert name == "self.display.set_mouth_sequence"
    assert set(arguments.keys()) == {"steps_json"}
    assert json.loads(arguments["steps_json"]) == steps


_WS2812_PORTS = (
    ("port_b", "Port B", "GPIO 9"),
    ("port_c", "Port C", "GPIO 17"),
)


def _ws2812_tool_names(port: str) -> tuple[str, ...]:
    return (
        f"{port}_ws2812_init",
        f"{port}_ws2812_set_pixel",
        f"{port}_ws2812_set_strip",
        f"{port}_ws2812_refresh",
        f"{port}_ws2812_clear",
    )


@pytest.mark.asyncio
@pytest.mark.parametrize(("port", "port_label", "gpio_label"), _WS2812_PORTS)
async def test_list_tools_includes_ws2812_tools_with_schemas(
    port,
    port_label,
    gpio_label,
):
    """Port B/C WS2812 wrappers are exposed with LLM-facing schemas."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools_by_name = {tool.name: tool for tool in result.root.tools}
    for tool_name in _ws2812_tool_names(port):
        assert tool_name in tools_by_name, f"{tool_name} tool should be registered"
        description = tools_by_name[tool_name].description
        assert port_label in description
        assert gpio_label in description
        assert "3.3 V CMOS data" in description
        assert "level shifter" in description

    init_schema = tools_by_name[f"{port}_ws2812_init"].inputSchema
    assert init_schema["properties"]["led_count"] == {
        "type": "integer",
        "description": "Number of LEDs in the strip (1..256).",
        "minimum": 1,
        "maximum": 256,
    }
    assert init_schema["properties"]["color_order"] == {
        "type": "string",
        "enum": ["grb", "rgb"],
        "default": "grb",
        "description": (
            "Logical LED color order. Use grb for standard WS2812/NeoPixel "
            "strips, or rgb for RGB-wired LEDs; the gateway swaps R/G before "
            "forwarding colors to the firmware."
        ),
    }
    assert init_schema["required"] == ["led_count"]

    pixel_schema = tools_by_name[f"{port}_ws2812_set_pixel"].inputSchema
    assert pixel_schema["properties"]["index"]["minimum"] == 0
    assert pixel_schema["properties"]["index"]["maximum"] == 255
    for channel in ("r", "g", "b"):
        assert pixel_schema["properties"][channel]["minimum"] == 0
        assert pixel_schema["properties"][channel]["maximum"] == 255
    assert pixel_schema["properties"]["refresh"] == {
        "type": "boolean",
        "description": "True to latch the update immediately.",
        "default": False,
    }
    assert pixel_schema["required"] == ["index", "r", "g", "b"]

    strip_schema = tools_by_name[f"{port}_ws2812_set_strip"].inputSchema
    colors_schema = strip_schema["properties"]["colors"]
    assert colors_schema["type"] == "array"
    assert colors_schema["minItems"] == 1
    assert colors_schema["maxItems"] == 256
    assert colors_schema["items"]["type"] == "array"
    assert colors_schema["items"]["minItems"] == 3
    assert colors_schema["items"]["maxItems"] == 3
    assert colors_schema["items"]["items"] == {
        "type": "integer",
        "minimum": 0,
        "maximum": 255,
    }
    assert strip_schema["required"] == ["colors"]

    for tool_name in (f"{port}_ws2812_refresh", f"{port}_ws2812_clear"):
        assert _schema_without_device_id(tools_by_name[tool_name]) == {
            "type": "object",
            "properties": {},
        }


_PORT_A_I2C_TOOL_NAMES = ("i2c_read", "i2c_write", "i2c_write_read")


@pytest.mark.asyncio
async def test_list_tools_port_a_i2c_declares_scl_speed_hz_schema():
    """Port A I2C wrappers expose the per-transaction clock schema."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools_by_name = {tool.name: tool for tool in result.root.tools}
    for tool_name in _PORT_A_I2C_TOOL_NAMES:
        assert tool_name in tools_by_name, f"{tool_name} tool should be registered"
        description = tools_by_name[tool_name].description
        assert "scl_speed_hz" in description
        assert "400000" in description
        assert "RCWL-9620" in description
        assert "ESP_ERR_INVALID_STATE" in description

        schema = tools_by_name[tool_name].inputSchema
        assert schema["properties"]["scl_speed_hz"] == {
            "type": "integer",
            "default": 400000,
            "description": (
                "I2C clock for this transaction. Default 400000; lower it "
                "(e.g. 100000 or 200000) for slower Units such as the "
                "RCWL-9620 ultrasonic ranger that fail at 400 kHz with "
                "ESP_ERR_INVALID_STATE."
            ),
            "minimum": 100000,
            "maximum": 1000000,
        }
        assert "scl_speed_hz" not in schema["required"]


@pytest.mark.asyncio
async def test_i2c_read_relays_scl_speed_hz_to_firmware(monkeypatch):
    """i2c_read forwards optional scl_speed_hz unchanged to the ESP32 tool."""
    calls: list[tuple[str, dict]] = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, tool_name, arguments, *, device_id=None):
            calls.append((tool_name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps({"ok": True, "bytes": [1, 2]}),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    import stackchan_mcp.stdio_server as stdio_server

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()
    arguments = {"addr": 0x57, "n_bytes": 2, "scl_speed_hz": 200000}

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "i2c_read", "arguments": arguments},
        )
    )

    assert calls == [("self.i2c.read", arguments)]
    assert json.loads(result.root.content[0].text) == {"ok": True, "bytes": [1, 2]}


def _make_ws2812_fake_gateway(monkeypatch):
    calls: list[tuple[str, dict]] = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, tool_name, arguments, *, device_id=None):
            calls.append((tool_name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps({"ok": True}),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    import stackchan_mcp.stdio_server as stdio_server

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    return calls


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("gateway_name", "request_args", "firmware_name", "firmware_args"),
    [
        (
            "port_b_ws2812_init",
            {"led_count": 18},
            "self.port_b.ws2812.init",
            {"led_count": 18},
        ),
        (
            "port_b_ws2812_set_pixel",
            {"index": 2, "r": 10, "g": 20, "b": 30, "refresh": True},
            "self.port_b.ws2812.set_pixel",
            {"index": 2, "r": 10, "g": 20, "b": 30, "refresh": True},
        ),
        (
            "port_b_ws2812_refresh",
            {},
            "self.port_b.ws2812.refresh",
            {},
        ),
        (
            "port_b_ws2812_clear",
            {},
            "self.port_b.ws2812.clear",
            {},
        ),
        (
            "port_c_ws2812_init",
            {"led_count": 18},
            "self.port_c.ws2812.init",
            {"led_count": 18},
        ),
        (
            "port_c_ws2812_set_pixel",
            {"index": 2, "r": 10, "g": 20, "b": 30, "refresh": True},
            "self.port_c.ws2812.set_pixel",
            {"index": 2, "r": 10, "g": 20, "b": 30, "refresh": True},
        ),
        (
            "port_c_ws2812_refresh",
            {},
            "self.port_c.ws2812.refresh",
            {},
        ),
        (
            "port_c_ws2812_clear",
            {},
            "self.port_c.ws2812.clear",
            {},
        ),
    ],
)
async def test_ws2812_tools_relay_to_firmware(
    monkeypatch,
    gateway_name,
    request_args,
    firmware_name,
    firmware_args,
):
    calls = _make_ws2812_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": gateway_name, "arguments": request_args},
        )
    )

    assert calls == [(firmware_name, firmware_args)]
    assert json.loads(result.root.content[0].text) == {"ok": True}


@pytest.mark.asyncio
@pytest.mark.parametrize("port", ["port_b", "port_c"])
async def test_ws2812_set_strip_relays_colors_as_json_string(monkeypatch, port):
    calls = _make_ws2812_fake_gateway(monkeypatch)
    server = create_server()
    colors = [[32, 0, 0], [0, 32, 0], [0, 0, 32]]

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_set_strip",
                "arguments": {"colors": colors},
            },
        )
    )

    assert len(calls) == 1
    name, arguments = calls[0]
    assert name == f"self.{port}.ws2812.set_strip"
    assert set(arguments.keys()) == {"colors"}
    assert json.loads(arguments["colors"]) == colors
    assert json.loads(result.root.content[0].text) == {"ok": True}


@pytest.mark.asyncio
@pytest.mark.parametrize("port", ["port_b", "port_c"])
async def test_ws2812_rgb_color_order_swaps_channels_until_reinitialized(
    monkeypatch,
    port,
):
    calls = _make_ws2812_fake_gateway(monkeypatch)
    server = create_server()

    await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_init",
                "arguments": {"led_count": 18, "color_order": "rgb"},
            },
        )
    )
    await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_set_pixel",
                "arguments": {
                    "index": 2,
                    "r": 255,
                    "g": 0,
                    "b": 64,
                    "refresh": True,
                },
            },
        )
    )
    await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_set_strip",
                "arguments": {"colors": [[255, 0, 64], [0, 16, 32]]},
            },
        )
    )
    await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_init",
                "arguments": {"led_count": 18},
            },
        )
    )
    await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_set_pixel",
                "arguments": {"index": 3, "r": 7, "g": 8, "b": 9},
            },
        )
    )
    await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_set_strip",
                "arguments": {"colors": [[7, 8, 9]]},
            },
        )
    )

    assert calls == [
        (f"self.{port}.ws2812.init", {"led_count": 18}),
        (
            f"self.{port}.ws2812.set_pixel",
            {"index": 2, "r": 0, "g": 255, "b": 64, "refresh": True},
        ),
        (
            f"self.{port}.ws2812.set_strip",
            {"colors": json.dumps([[0, 255, 64], [16, 0, 32]])},
        ),
        (f"self.{port}.ws2812.init", {"led_count": 18}),
        (f"self.{port}.ws2812.set_pixel", {"index": 3, "r": 7, "g": 8, "b": 9}),
        (f"self.{port}.ws2812.set_strip", {"colors": json.dumps([[7, 8, 9]])}),
    ]


@pytest.mark.asyncio
@pytest.mark.parametrize("port", ["port_b", "port_c"])
async def test_ws2812_init_rejects_invalid_color_order(monkeypatch, port):
    calls = _make_ws2812_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": f"{port}_ws2812_init",
                "arguments": {"led_count": 18, "color_order": "bgr"},
            },
        )
    )

    assert calls == []
    assert "Input validation error" in result.root.content[0].text
    assert "'bgr' is not one of ['grb', 'rgb']" in result.root.content[0].text


# ---------------------------------------------------------------------------
# move_head — Issue #109: schema + handler enforce the M5Stack-recommended
# pitch operating range (5..85). pitch=0 motion-starts have been observed on
# device to trigger the SCS0009 bus hang tracked in Issue #100, and the
# firmware-side `set_head_angles` tool remains the documented escape hatch
# for callers that need the wider firmware hard clamp (0..88).
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_list_tools_move_head_declares_recommended_pitch_range():
    """move_head schema mirrors M5Stack-recommended 5..85 / yaw -90..90."""
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tool = next((t for t in result.root.tools if t.name == "move_head"), None)
    assert tool is not None, "move_head tool should be registered"

    pitch_schema = tool.inputSchema["properties"]["pitch"]
    assert pitch_schema["minimum"] == 5
    assert pitch_schema["maximum"] == 85

    yaw_schema = tool.inputSchema["properties"]["yaw"]
    assert yaw_schema["minimum"] == -90
    assert yaw_schema["maximum"] == 90

    # The description should mention the escape-hatch tool name so an LLM
    # reading it can pick the right alternative for permissive use cases.
    assert "set_head_angles" in tool.description

    speed_schema = tool.inputSchema["properties"]["speed"]
    assert speed_schema["oneOf"] == [
        {"enum": ["low", "mid", "high"]},
        {"type": "integer", "minimum": 1, "maximum": 10000},
    ]
    assert speed_schema["description"] == SPEED_DESCRIPTION


@pytest.mark.parametrize(
    ("speed", "expected_dps"),
    [
        ("low", 30),
        ("mid", 120),
        ("high", 240),
        (None, None),
        (200, 200),
        (1, 1),
        (10000, 10000),
    ],
)
def test_resolve_speed_dps_valid(speed, expected_dps):
    assert _resolve_speed_dps(speed) == expected_dps


@pytest.mark.parametrize(
    "bad_speed",
    ["fast", "slow", "", 0, -1, 10001, True, False, 1.5, [120], {}],
)
def test_resolve_speed_dps_invalid(bad_speed):
    with pytest.raises((ValueError, TypeError)):
        _resolve_speed_dps(bad_speed)


def _make_fake_gateway(monkeypatch):
    """Helper: wire a FakeESP32/FakeGateway into the stdio_server module.

    Returns the ``calls`` list that records each ESP32 call. The handler
    treats this device as connected. Used by the move_head handler tests.
    """
    calls: list[tuple[str, dict]] = []

    class FakeESP32:
        device_connected = True

        async def call_tool(self, tool_name, arguments, *, device_id=None):
            calls.append((tool_name, arguments))
            return {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(
                            {"yaw": arguments.get("yaw", 0), "pitch": arguments.get("pitch", 0)}
                        ),
                    }
                ],
            }, None

    class FakeGateway:
        esp32 = FakeESP32()

    import stackchan_mcp.stdio_server as stdio_server

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    return calls


_MISSING = object()


def _move_head_request(yaw, pitch, speed=_MISSING):
    arguments = {"yaw": yaw, "pitch": pitch}
    if speed is not _MISSING:
        arguments["speed"] = speed
    return CallToolRequest(
        method="tools/call",
        params={"name": "move_head", "arguments": arguments},
    )


def _assert_rejected_without_dispatch(result, calls):
    """Common shape: out-of-range request is refused and no ESP32 call fires.

    Two refusal paths coexist:
    - mcp SDK server-side JSON Schema validation rejects the request before
      the handler runs (current behaviour observed with mcp>=1.0). The
      response text is a human-readable validation message rather than the
      handler's structured JSON error.
    - The handler's belt-and-suspenders validation in stdio_server.py
      returns a clean ``{"error": "..."}`` JSON for SDK versions or future
      configurations that may not enforce the schema bounds.

    Either path is acceptable. What matters for hardware safety is that
    ``self.robot.set_head_angles`` is never called.
    """
    assert calls == [], (
        "Out-of-range move_head must not dispatch a motion call. "
        f"Got calls={calls}, response text={result.root.content[0].text!r}"
    )
    response_text = result.root.content[0].text
    # The response should signal an error in some shape. Either the handler
    # JSON ({"error": "..."}) or the SDK validation prose mentions one of
    # these keywords.
    lower = response_text.lower()
    assert any(
        keyword in lower
        for keyword in ("error", "invalid", "minimum", "maximum", "type")
    ), f"Expected an error signal in {response_text!r}"


@pytest.mark.asyncio
@pytest.mark.parametrize("pitch", [4, 86, None, "45", 5.5, True, False])
async def test_move_head_rejects_invalid_pitch_without_motion(monkeypatch, pitch):
    """Reject out-of-range and non-integer pitch without dispatching motion."""
    calls = _make_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _move_head_request(yaw=0, pitch=pitch)
    )

    _assert_rejected_without_dispatch(result, calls)


@pytest.mark.asyncio
@pytest.mark.parametrize("yaw", [-91, 91, 200, -1000])
async def test_move_head_rejects_yaw_out_of_range(monkeypatch, yaw):
    """yaw values outside -90..+90 are refused."""
    calls = _make_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _move_head_request(yaw=yaw, pitch=45)
    )

    _assert_rejected_without_dispatch(result, calls)


@pytest.mark.asyncio
@pytest.mark.parametrize("pitch", [5, 45, 85])
async def test_move_head_accepts_pitch_inside_recommended(monkeypatch, pitch):
    """Boundary and mid-range pitch values are accepted and relayed."""
    calls = _make_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _move_head_request(yaw=0, pitch=pitch)
    )

    assert len(calls) == 1
    name, arguments = calls[0]
    assert name == "self.robot.set_head_angles"
    assert arguments == {"yaw": 0, "pitch": pitch}

    payload = json.loads(result.root.content[0].text)
    assert "error" not in payload


@pytest.mark.asyncio
async def test_move_head_speed_mid_forwards_speed_dps(monkeypatch):
    calls = _make_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _move_head_request(yaw=10, pitch=45, speed="mid")
    )

    assert len(calls) == 1
    name, arguments = calls[0]
    assert name == "self.robot.set_head_angles"
    assert arguments == {"yaw": 10, "pitch": 45, "speed_dps": 120}

    payload = json.loads(result.root.content[0].text)
    assert "error" not in payload


@pytest.mark.asyncio
async def test_move_head_without_speed_omits_speed_dps(monkeypatch):
    calls = _make_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _move_head_request(yaw=10, pitch=45)
    )

    assert len(calls) == 1
    name, arguments = calls[0]
    assert name == "self.robot.set_head_angles"
    assert arguments == {"yaw": 10, "pitch": 45}

    payload = json.loads(result.root.content[0].text)
    assert "error" not in payload


# ---------------------------------------------------------------------------
# Stack-chan event notification config: capabilities, instructions, allowlist
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    (
        "legacy",
        "channels",
        "jsonl",
        "expected_capabilities",
        "expected_instructions",
    ),
    [
        (
            False,
            True,
            False,
            {CHANNEL_CAPABILITY: {}},
            STACKCHAN_CHANNEL_INSTRUCTIONS,
        ),
        (
            True,
            False,
            False,
            {STACKCHAN_EVENT_METHOD: {}},
            STACKCHAN_EVENT_INSTRUCTIONS,
        ),
        (
            False,
            False,
            True,
            {},
            STACKCHAN_JSONL_INSTRUCTIONS,
        ),
        (
            True,
            True,
            False,
            {STACKCHAN_EVENT_METHOD: {}, CHANNEL_CAPABILITY: {}},
            STACKCHAN_CHANNEL_INSTRUCTIONS + "\n\n" + STACKCHAN_EVENT_INSTRUCTIONS,
        ),
        (
            False,
            False,
            False,
            {},
            None,
        ),
    ],
)
def test_stackchan_event_capabilities_and_instructions_follow_notify_config(
    legacy,
    channels,
    jsonl,
    expected_capabilities,
    expected_instructions,
):
    config = _notify_config(legacy=legacy, channels=channels, jsonl=jsonl)
    server = create_server()
    options = _create_initialization_options(server, notify_config=config)

    assert _build_experimental_capabilities(config) == expected_capabilities
    assert options.capabilities.experimental == expected_capabilities
    assert _build_stackchan_event_instructions(config) == expected_instructions
    assert options.instructions == expected_instructions


def test_create_initialization_options_requires_notify_config():
    server = create_server(notify_config=_notify_config())

    with pytest.raises(TypeError):
        _create_initialization_options(server)


def test_create_initialization_options_uses_explicit_notify_config(monkeypatch):
    all_off_config = _notify_config(legacy=False, channels=False, jsonl=False)
    channels_config = _notify_config(legacy=False, channels=True, jsonl=False)

    load_calls = []

    def load_all_off_config():
        load_calls.append("load")
        return all_off_config

    monkeypatch.setattr(stdio_server, "load_notify_config", load_all_off_config)
    server = create_server(notify_config=all_off_config)

    all_off_options = _create_initialization_options(server, all_off_config)
    channels_options = _create_initialization_options(server, channels_config)

    assert load_calls == []
    assert all_off_options.capabilities.experimental == {}
    assert all_off_options.instructions is None
    assert channels_options.capabilities.experimental == {CHANNEL_CAPABILITY: {}}
    assert channels_options.instructions == STACKCHAN_CHANNEL_INSTRUCTIONS


@pytest.mark.asyncio
async def test_notify_stackchan_event_accepts_channel_method(monkeypatch):
    session = _FakeNotificationSession()
    monkeypatch.setattr("stackchan_mcp.stdio_server._active_session", session)
    monkeypatch.setattr("stackchan_mcp.stdio_server._active_sessions", {})

    params = {"content": "(head pat)", "meta": {"action": "head_pat"}}
    await notify_stackchan_event(CHANNEL_NOTIFICATION_METHOD, params)

    assert session.notifications == [
        {"method": CHANNEL_NOTIFICATION_METHOD, "params": params}
    ]


@pytest.mark.asyncio
async def test_notify_stackchan_event_rejects_unsupported_method(
    monkeypatch,
    caplog,
):
    session = _FakeNotificationSession()
    monkeypatch.setattr("stackchan_mcp.stdio_server._active_session", session)
    monkeypatch.setattr("stackchan_mcp.stdio_server._active_sessions", {})

    with caplog.at_level("WARNING"):
        await notify_stackchan_event("notifications/other", {"ok": True})

    assert session.notifications == []
    assert "Unsupported stackchan event notification method" in caplog.text


def _notify_config(
    *,
    legacy: bool = False,
    channels: bool = False,
    jsonl: bool = False,
) -> NotifyConfig:
    return NotifyConfig(
        legacy_event_enabled=legacy,
        channels_enabled=channels,
        jsonl_enabled=jsonl,
        jsonl_path=Path("/tmp/stackchan-events-test.jsonl"),
        messages=dict(DEFAULT_MESSAGE_TEMPLATES),
    )


class _FakeNotificationSession:
    def __init__(self):
        self.notifications = []

    async def send_notification(self, notification):
        self.notifications.append(
            notification.model_dump(
                by_alias=True,
                mode="json",
                exclude_none=True,
            )
        )


def _make_follow_pose_fake_gateway(monkeypatch):
    """Helper: capture the FollowPoseStreamConfig that reaches start_follow.

    Returns a single-element ``captured`` list; the handler imports
    start_follow from follow_pose_stream at call time, so patching the
    module attribute is enough to intercept the config without driving a
    real WebSocket subscription.
    """
    captured: list = []

    class FakeGateway:
        pass

    async def fake_start_follow(gateway, cfg):
        captured.append(cfg)
        return {"running": True}

    import stackchan_mcp.follow_pose_stream as follow_pose_stream

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(follow_pose_stream, "start_follow", fake_start_follow)
    return captured


def _follow_pose_request(**arguments):
    arguments.setdefault("action", "start")
    arguments.setdefault("url", "ws://example.test/pose")
    return CallToolRequest(
        method="tools/call",
        params={"name": "stackchan_follow_pose_stream", "arguments": arguments},
    )


@pytest.mark.asyncio
@pytest.mark.parametrize("window", [1, 5, 20])
async def test_follow_pose_smoothing_window_propagates(monkeypatch, window):
    """Explicit in-range smoothing_window reaches FollowPoseStreamConfig."""
    captured = _make_follow_pose_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_pose_request(smoothing_window=window)
    )

    assert len(captured) == 1, (
        f"start_follow should fire once for smoothing_window={window}; "
        f"response text={result.root.content[0].text!r}"
    )
    assert captured[0].smoothing_window == window


@pytest.mark.asyncio
async def test_follow_pose_smoothing_window_defaults_to_five(monkeypatch):
    """Omitting smoothing_window reproduces the dataclass default of 5."""
    captured = _make_follow_pose_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_pose_request()
    )

    assert len(captured) == 1, (
        "start_follow should fire once when smoothing_window is omitted; "
        f"response text={result.root.content[0].text!r}"
    )
    assert captured[0].smoothing_window == 5


@pytest.mark.asyncio
async def test_follow_pose_explicit_smoothing_window_wins_over_user_default(
    monkeypatch,
    tmp_path,
):
    """Explicit MCP args override user-defaults TOML values."""
    import stackchan_mcp.user_defaults as user_defaults

    config_dir = tmp_path / "config"
    config_dir.mkdir()
    (config_dir / "user-defaults.toml").write_text(
        "[tool.stackchan_follow_pose_stream]\n"
        "smoothing_window = 1\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(
        user_defaults.platformdirs,
        "user_config_path",
        lambda appname, **kwargs: config_dir,
    )
    user_defaults._clear_user_defaults_cache_for_tests()

    try:
        captured = _make_follow_pose_fake_gateway(monkeypatch)
        server = create_server()

        result = await server.request_handlers[CallToolRequest](
            _follow_pose_request(smoothing_window=20)
        )
    finally:
        user_defaults._clear_user_defaults_cache_for_tests()

    assert len(captured) == 1, (
        "start_follow should fire once with the explicit argument; "
        f"response text={result.root.content[0].text!r}"
    )
    assert captured[0].smoothing_window == 20


@pytest.mark.asyncio
@pytest.mark.parametrize("window", [0, 21, -1, 100])
async def test_follow_pose_smoothing_window_out_of_range_rejected(monkeypatch, window):
    """Out-of-range smoothing_window is refused without starting a follow."""
    captured = _make_follow_pose_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_pose_request(smoothing_window=window)
    )

    assert captured == [], (
        "Out-of-range smoothing_window must not start a follow. "
        f"Got captured={captured}, "
        f"response text={result.root.content[0].text!r}"
    )
    response_text = result.root.content[0].text.lower()
    assert any(
        keyword in response_text
        for keyword in ("error", "invalid", "minimum", "maximum", "type")
    ), f"Expected an error signal in {result.root.content[0].text!r}"


@pytest.mark.asyncio
async def test_follow_pose_smoothing_window_non_integer_rejected(monkeypatch):
    """A non-integer smoothing_window is refused without starting a follow."""
    captured = _make_follow_pose_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_pose_request(smoothing_window=3.5)
    )

    assert captured == [], (
        "Non-integer smoothing_window must not start a follow. "
        f"Got captured={captured}, "
        f"response text={result.root.content[0].text!r}"
    )
    response_text = result.root.content[0].text.lower()
    assert any(
        keyword in response_text
        for keyword in ("error", "invalid", "type", "integer")
    ), f"Expected an error signal in {result.root.content[0].text!r}"


def _make_follow_led_fake_gateway(monkeypatch):
    captured: list = []

    class FakeGateway:
        pass

    async def fake_start_follow(gateway, cfg):
        captured.append(cfg)
        return {
            "running": True,
            "target": cfg.target,
            "max_fps": cfg.max_fps,
        }

    import stackchan_mcp.follow_led_stream as follow_led_stream

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(follow_led_stream, "start_follow", fake_start_follow)
    return captured


def _follow_led_request(**arguments):
    arguments.setdefault("action", "start")
    arguments.setdefault("url", "ws://example.test/led")
    return CallToolRequest(
        method="tools/call",
        params={"name": "stackchan_follow_led_stream", "arguments": arguments},
    )


@pytest.mark.asyncio
async def test_list_tools_includes_follow_led_stream_schema():
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools = {tool.name: tool for tool in result.root.tools}
    tool = tools["stackchan_follow_led_stream"]
    schema = tool.inputSchema
    assert schema["properties"]["target"]["enum"] == [
        "base_ring",
        "port_b",
        "port_c",
    ]
    assert schema["properties"]["led_count"]["maximum"] == 256
    assert schema["properties"]["color_order"] == {
        "type": "string",
        "enum": ["grb", "rgb"],
        "default": "grb",
        "description": (
            "WS2812 strip color order for target=port_b or target=port_c. "
            "Use rgb for RGB-wired LEDs; the gateway swaps R/G before "
            "forwarding to the firmware. base_ring only supports grb."
        ),
    }
    assert schema["properties"]["max_fps"]["maximum"] == 30
    assert "kind='event'" in tool.description


@pytest.mark.asyncio
async def test_list_tools_includes_beat_mode_tools():
    server = create_server()

    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )

    tools = {tool.name: tool for tool in result.root.tools}
    for name in (
        "beat_mode_start",
        "beat_mode_stop",
        "beat_mode_update",
        "beat_meta_snapshot",
        "beat_clip_save",
    ):
        assert name in tools

    start_schema = tools["beat_mode_start"].inputSchema
    assert start_schema["properties"]["motion_intensity"]["maximum"] == 1
    assert start_schema["properties"]["sensitivity"]["default"] == 0.5
    assert start_schema["properties"]["sensitivity"]["maximum"] == 1
    sensitivity_description = start_schema["properties"]["sensitivity"]["description"]
    assert "0.5 => 0.004" in sensitivity_description
    assert start_schema["properties"]["color"]["minItems"] == 3
    assert "listen() calls fail fast" in tools["beat_mode_start"].description
    assert "base ring" in tools["beat_mode_start"].description

    update_schema = tools["beat_mode_update"].inputSchema
    assert update_schema["properties"]["sensitivity"]["minimum"] == 0
    assert update_schema["properties"]["blink_rate"]["minimum"] == 0.25
    assert update_schema["properties"]["motion_enabled"]["type"] == "boolean"
    assert "persists on disk" in tools["beat_clip_save"].description
    assert "caller is responsible" in tools["beat_clip_save"].description


@pytest.mark.asyncio
async def test_beat_mode_start_arguments_propagate(monkeypatch):
    captured = {}

    class FakeGateway:
        pass

    async def fake_start_beat_mode(gateway, cfg):
        captured["gateway"] = gateway
        captured["cfg"] = cfg
        return {
            "active": True,
            "motion": {"intensity": cfg.motion_intensity},
            "sensitivity": cfg.sensitivity,
            "min_onset_rms": cfg.min_onset_rms,
            "led": {"color": list(cfg.color)},
        }

    import stackchan_mcp.beat as beat

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(beat, "start_beat_mode", fake_start_beat_mode)

    server = create_server()
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": "beat_mode_start",
                "arguments": {
                    "motion_intensity": 0.75,
                    "sensitivity": 0.25,
                    "color": [10, 20, 30],
                    "duration_sec": 12,
                },
            },
        )
    )

    payload = json.loads(result.root.content[0].text)
    assert payload["ok"] is True
    assert captured["cfg"].motion_intensity == 0.75
    assert captured["cfg"].sensitivity == 0.25
    assert captured["cfg"].color == (10, 20, 30)
    assert captured["cfg"].duration_sec == 12


@pytest.mark.asyncio
async def test_beat_mode_start_rejects_invalid_sensitivity(monkeypatch):
    class FakeGateway:
        pass

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": "beat_mode_start",
                "arguments": {"sensitivity": -0.1},
            },
        )
    )

    assert "Input validation error" in result.root.content[0].text
    assert "less than the minimum of 0" in result.root.content[0].text


@pytest.mark.asyncio
async def test_beat_mode_update_rejects_invalid_color(monkeypatch):
    class FakeGateway:
        pass

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": "beat_mode_update",
                "arguments": {"color": [255, 0]},
            },
        )
    )

    assert "Input validation error" in result.root.content[0].text
    assert "too short" in result.root.content[0].text


@pytest.mark.asyncio
async def test_beat_mode_update_rejects_invalid_sensitivity(monkeypatch):
    class FakeGateway:
        pass

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={
                "name": "beat_mode_update",
                "arguments": {"sensitivity": 1.5},
            },
        )
    )

    assert "Input validation error" in result.root.content[0].text
    assert "greater than the maximum of 1" in result.root.content[0].text


@pytest.mark.asyncio
async def test_beat_clip_save_defaults_to_ten_seconds(monkeypatch):
    captured = {}

    class FakeGateway:
        pass

    async def fake_save_beat_clip(seconds):
        captured["seconds"] = seconds
        return {"path": "/tmp/beat.wav", "seconds": 1.0}

    import stackchan_mcp.beat as beat

    monkeypatch.setattr(stdio_server, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(beat, "save_beat_clip", fake_save_beat_clip)

    server = create_server()
    result = await server.request_handlers[CallToolRequest](
        CallToolRequest(
            method="tools/call",
            params={"name": "beat_clip_save", "arguments": {}},
        )
    )

    payload = json.loads(result.root.content[0].text)
    assert payload["ok"] is True
    assert payload["path"] == "/tmp/beat.wav"
    assert captured["seconds"] == 10.0


@pytest.mark.asyncio
@pytest.mark.parametrize("target", ["port_b", "port_c"])
async def test_follow_led_ws2812_target_arguments_propagate(monkeypatch, target):
    captured = _make_follow_led_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_led_request(
            target=target,
            led_count=18,
            max_fps=24,
            color_order="rgb",
            source_filter="stage",
            frame_filter="calibrated",
            reconnect_initial_backoff_s=0.25,
            reconnect_max_backoff_s=2.0,
        )
    )

    assert len(captured) == 1, result.root.content[0].text
    cfg = captured[0]
    assert cfg.target == target
    assert cfg.led_count == 18
    assert cfg.max_fps == 24
    assert cfg.color_order == "rgb"
    assert cfg.source_filter == "stage"
    assert cfg.frame_filter == "calibrated"
    assert cfg.reconnect_initial_backoff_s == 0.25
    assert cfg.reconnect_max_backoff_s == 2.0


@pytest.mark.asyncio
async def test_follow_led_reads_user_defaults(monkeypatch, tmp_path):
    import stackchan_mcp.user_defaults as user_defaults

    config_dir = tmp_path / "config"
    config_dir.mkdir()
    (config_dir / "user-defaults.toml").write_text(
        "[tool.stackchan_follow_led_stream]\n"
        'target = "port_c"\n'
        "led_count = 7\n"
        "max_fps = 12\n"
        'color_order = "rgb"\n'
        'source_filter = "stage"\n',
        encoding="utf-8",
    )
    monkeypatch.setattr(
        user_defaults.platformdirs,
        "user_config_path",
        lambda appname, **kwargs: config_dir,
    )
    user_defaults._clear_user_defaults_cache_for_tests()

    try:
        captured = _make_follow_led_fake_gateway(monkeypatch)
        server = create_server()
        result = await server.request_handlers[CallToolRequest](
            _follow_led_request()
        )
    finally:
        user_defaults._clear_user_defaults_cache_for_tests()

    assert len(captured) == 1, result.root.content[0].text
    assert captured[0].target == "port_c"
    assert captured[0].led_count == 7
    assert captured[0].max_fps == 12
    assert captured[0].color_order == "rgb"
    assert captured[0].source_filter == "stage"


@pytest.mark.asyncio
async def test_follow_led_rejects_bad_target_led_count(monkeypatch):
    captured = _make_follow_led_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_led_request(target="base_ring", led_count=13)
    )

    assert captured == []
    payload = json.loads(result.root.content[0].text)
    assert payload["ok"] is False
    assert "base_ring" in payload["error"]


@pytest.mark.asyncio
async def test_follow_led_rejects_color_order_for_base_ring(monkeypatch):
    captured = _make_follow_led_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_led_request(target="base_ring", color_order="rgb")
    )

    assert captured == []
    payload = json.loads(result.root.content[0].text)
    assert payload["ok"] is False
    assert "color_order is only supported" in payload["error"]


@pytest.mark.asyncio
async def test_follow_led_rejects_bad_color_order(monkeypatch):
    captured = _make_follow_led_fake_gateway(monkeypatch)
    server = create_server()

    result = await server.request_handlers[CallToolRequest](
        _follow_led_request(target="port_b", led_count=8, color_order="bgr")
    )

    assert captured == []
    assert "Input validation error" in result.root.content[0].text
    assert "'bgr' is not one of ['grb', 'rgb']" in result.root.content[0].text


@pytest.mark.asyncio
async def test_routed_tools_publish_optional_device_id_schema():
    """Every per-call routed tool declares an optional string device_id."""
    from stackchan_mcp.stdio_server import DEVICE_ID_EXEMPT_TOOLS

    server = create_server()
    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )
    tools = {tool.name: tool for tool in result.root.tools}

    for name in ("say", "listen", "load_avatar_set", "move_head", "set_volume"):
        assert name in tools
    for name, tool in tools.items():
        props = tool.inputSchema.get("properties", {})
        if name in DEVICE_ID_EXEMPT_TOOLS:
            if name != "set_default_device":
                assert "device_id" not in props, name
            continue
        assert props.get("device_id", {}).get("type") == "string", name
        assert "device_id" not in tool.inputSchema.get("required", []), name


@pytest.mark.asyncio
async def test_exempt_tools_are_real_tool_names():
    from stackchan_mcp.stdio_server import DEVICE_ID_EXEMPT_TOOLS

    server = create_server()
    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )
    names = {tool.name for tool in result.root.tools}
    assert DEVICE_ID_EXEMPT_TOOLS <= names
