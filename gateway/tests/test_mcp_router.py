"""Tests for MCP router — initialize, tools/list, tools/call (local stub mode)."""

import json

from stackchan_mcp.mcp_router import route


def test_initialize() -> None:
    resp = route({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
    assert resp["id"] == 1
    assert "error" not in resp
    result = resp["result"]
    assert result["serverInfo"]["name"] == "stackchan-mcp"
    assert "capabilities" in result


def test_tools_list() -> None:
    resp = route({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
    assert resp["id"] == 2
    tools = resp["result"]["tools"]
    names = [t["name"] for t in tools]
    assert "self.robot.get_head_angles" in names
    assert "self.robot.set_head_angles" in names
    assert "self.robot.set_led_color" in names
    assert "self.audio_speaker.set_volume" in names
    assert "self.camera.take_photo" in names
    assert "self.get_device_status" in names


def test_get_head_angles() -> None:
    resp = route(
        {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {"name": "self.robot.get_head_angles", "arguments": {}},
        }
    )
    assert resp["id"] == 3
    assert "error" not in resp
    angles = json.loads(resp["result"]["content"][0]["text"])
    assert "yaw" in angles
    assert "pitch" in angles


def test_set_head_angles() -> None:
    resp = route(
        {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/call",
            "params": {
                "name": "self.robot.set_head_angles",
                "arguments": {"yaw": 30, "pitch": -15, "speed": 80},
            },
        }
    )
    assert resp["id"] == 4
    assert "error" not in resp

    # Verify state persisted
    resp2 = route(
        {
            "jsonrpc": "2.0",
            "id": 5,
            "method": "tools/call",
            "params": {"name": "self.robot.get_head_angles", "arguments": {}},
        }
    )
    angles = json.loads(resp2["result"]["content"][0]["text"])
    assert angles["yaw"] == 30
    assert angles["pitch"] == -15


def test_set_led_color() -> None:
    resp = route(
        {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "tools/call",
            "params": {
                "name": "self.robot.set_led_color",
                "arguments": {"r": 255, "g": 128, "b": 0},
            },
        }
    )
    assert resp["id"] == 6
    assert "error" not in resp


def test_set_volume() -> None:
    resp = route(
        {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "tools/call",
            "params": {
                "name": "self.audio_speaker.set_volume",
                "arguments": {"volume": 75},
            },
        }
    )
    assert resp["id"] == 7
    assert "error" not in resp


def test_unknown_tool() -> None:
    resp = route(
        {
            "jsonrpc": "2.0",
            "id": 8,
            "method": "tools/call",
            "params": {"name": "nonexistent.tool", "arguments": {}},
        }
    )
    assert resp["id"] == 8
    assert "error" in resp
    assert resp["error"]["code"] == -32601


def test_unknown_method() -> None:
    resp = route({"jsonrpc": "2.0", "id": 9, "method": "unknown/method", "params": {}})
    assert resp["id"] == 9
    assert "error" in resp
