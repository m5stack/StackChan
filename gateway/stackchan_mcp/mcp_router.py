"""JSON-RPC 2.0 router for MCP protocol.

Handles: initialize, tools/list, tools/call

In gateway mode, tools/call is relayed to the ESP32 device.
For testing without ESP32, use route() with local stub handlers.
"""

from __future__ import annotations

import json
import logging
from typing import Any

from .tools import TOOL_DEFINITIONS

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# MCP server capabilities
# ---------------------------------------------------------------------------

SERVER_INFO = {
    "name": "stackchan-mcp",
    "version": "0.1.0",
}

SERVER_CAPABILITIES = {
    "tools": {},
}


# ---------------------------------------------------------------------------
# JSON-RPC helpers
# ---------------------------------------------------------------------------


def _ok(req_id: Any, result: Any) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": req_id, "result": result}


def _error(req_id: Any, code: int, message: str) -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "error": {"code": code, "message": message},
    }


# ---------------------------------------------------------------------------
# Router (local stub mode for testing)
# ---------------------------------------------------------------------------

# Lazy import to avoid circular dependency
_TOOL_HANDLERS: dict[str, Any] | None = None


def _get_tool_handlers() -> dict[str, Any]:
    global _TOOL_HANDLERS
    if _TOOL_HANDLERS is None:
        from .handlers.audio import set_volume
        from .handlers.robot import get_head_angles, set_head_angles, set_led_color

        _TOOL_HANDLERS = {
            "self.robot.get_head_angles": get_head_angles,
            "self.robot.set_head_angles": set_head_angles,
            "self.robot.set_led_color": set_led_color,
            "self.audio_speaker.set_volume": set_volume,
        }
    return _TOOL_HANDLERS


def route(payload: dict[str, Any]) -> dict[str, Any]:
    """Route a single JSON-RPC 2.0 request and return a response dict.

    This is the local stub mode — tool calls are handled in-process.
    """
    req_id = payload.get("id")
    method = payload.get("method", "")
    params = payload.get("params", {})

    logger.info("mcp method=%s id=%s", method, req_id)

    if method == "initialize":
        return _ok(
            req_id,
            {
                "protocolVersion": "2024-11-05",
                "serverInfo": SERVER_INFO,
                "capabilities": SERVER_CAPABILITIES,
            },
        )

    if method == "tools/list":
        return _ok(req_id, {"tools": TOOL_DEFINITIONS})

    if method == "tools/call":
        tool_name = params.get("name", "")
        arguments = params.get("arguments", {})
        handlers = _get_tool_handlers()
        handler = handlers.get(tool_name)

        if handler is None:
            return _error(req_id, -32601, f"Unknown tool: {tool_name}")

        try:
            result = handler(arguments) if arguments else handler()
            return _ok(
                req_id,
                {
                    "content": [
                        {
                            "type": "text",
                            "text": json.dumps(result)
                            if not isinstance(result, str)
                            else result,
                        }
                    ],
                },
            )
        except Exception as exc:
            logger.exception("tools/call %s failed", tool_name)
            return _error(req_id, -32000, str(exc))

    return _error(req_id, -32601, f"Method not found: {method}")
