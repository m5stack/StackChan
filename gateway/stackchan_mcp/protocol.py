"""xiaozhi-esp32 protocol definitions.

Defines message formats for communication between the gateway and ESP32 device.
Based on: xiaozhi-esp32/docs/mcp-protocol.md
"""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field


# ---------------------------------------------------------------------------
# Transport-level messages (hello handshake)
# ---------------------------------------------------------------------------


class AudioParams(BaseModel):
    format: str = "opus"
    sample_rate: int = 16000
    channels: int = 1
    frame_duration: int = 60


class HelloMessage(BaseModel):
    """ESP32 -> Gateway: hello (connection announcement)."""

    type: str = "hello"
    version: int = 1
    features: dict[str, Any] = Field(default_factory=lambda: {"mcp": True})
    transport: str = "websocket"
    audio_params: AudioParams = Field(default_factory=AudioParams)
    session_id: str | None = None


class HelloResponse(BaseModel):
    """Gateway -> ESP32: hello response."""

    type: str = "hello"
    version: int = 1
    transport: str = "websocket"
    session_id: str | None = None


# ---------------------------------------------------------------------------
# MCP message wrapper (over transport)
# ---------------------------------------------------------------------------


class McpMessage(BaseModel):
    """MCP message wrapper for transport.

    All MCP communication is wrapped in this envelope.
    """

    session_id: str = ""
    type: str = "mcp"
    payload: dict[str, Any] = Field(default_factory=dict)


# ---------------------------------------------------------------------------
# JSON-RPC 2.0 helpers
# ---------------------------------------------------------------------------


def make_jsonrpc_request(method: str, params: dict[str, Any], req_id: int) -> dict[str, Any]:
    """Create a JSON-RPC 2.0 request payload."""
    return {
        "jsonrpc": "2.0",
        "method": method,
        "params": params,
        "id": req_id,
    }


def make_mcp_message(
    session_id: str, method: str, params: dict[str, Any], req_id: int
) -> dict[str, Any]:
    """Create a full MCP transport message with JSON-RPC payload."""
    return {
        "session_id": session_id,
        "type": "mcp",
        "payload": make_jsonrpc_request(method, params, req_id),
    }


def parse_jsonrpc_response(payload: dict[str, Any]) -> tuple[Any, dict[str, Any] | None]:
    """Parse a JSON-RPC 2.0 response.

    Returns (result, error) — one of them will be None.
    """
    if "error" in payload:
        return None, payload["error"]
    return payload.get("result"), None
