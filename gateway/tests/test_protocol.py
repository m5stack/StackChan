"""Tests for protocol message definitions."""

from stackchan_mcp.protocol import (
    HelloMessage,
    HelloResponse,
    McpMessage,
    make_jsonrpc_request,
    make_mcp_message,
    parse_jsonrpc_response,
)


def test_hello_message() -> None:
    msg = HelloMessage()
    assert msg.type == "hello"
    assert msg.version == 1
    assert msg.features["mcp"] is True
    assert msg.transport == "websocket"


def test_hello_message_from_dict() -> None:
    data = {
        "type": "hello",
        "version": 1,
        "features": {"mcp": True},
        "transport": "websocket",
        "audio_params": {
            "format": "opus",
            "sample_rate": 16000,
            "channels": 1,
            "frame_duration": 60,
        },
    }
    msg = HelloMessage(**data)
    assert msg.features["mcp"] is True
    assert msg.audio_params.format == "opus"
    assert msg.audio_params.sample_rate == 16000


def test_hello_response() -> None:
    resp = HelloResponse(session_id="test-session")
    dumped = resp.model_dump()
    assert dumped["type"] == "hello"
    assert dumped["version"] == 1
    assert dumped["session_id"] == "test-session"


def test_mcp_message() -> None:
    msg = McpMessage(
        session_id="sess-1",
        payload={"jsonrpc": "2.0", "method": "initialize", "params": {}, "id": 1},
    )
    assert msg.type == "mcp"
    assert msg.session_id == "sess-1"
    assert msg.payload["method"] == "initialize"


def test_make_jsonrpc_request() -> None:
    req = make_jsonrpc_request("tools/call", {"name": "test", "arguments": {}}, 42)
    assert req["jsonrpc"] == "2.0"
    assert req["method"] == "tools/call"
    assert req["id"] == 42
    assert req["params"]["name"] == "test"


def test_make_mcp_message() -> None:
    msg = make_mcp_message("sess-1", "initialize", {"capabilities": {}}, 1)
    assert msg["session_id"] == "sess-1"
    assert msg["type"] == "mcp"
    assert msg["payload"]["method"] == "initialize"
    assert msg["payload"]["id"] == 1


def test_parse_jsonrpc_response_success() -> None:
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "result": {"protocolVersion": "2024-11-05"},
    }
    result, error = parse_jsonrpc_response(payload)
    assert result == {"protocolVersion": "2024-11-05"}
    assert error is None


def test_parse_jsonrpc_response_error() -> None:
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "error": {"code": -32601, "message": "Method not found"},
    }
    result, error = parse_jsonrpc_response(payload)
    assert result is None
    assert error["code"] == -32601
    assert error["message"] == "Method not found"
