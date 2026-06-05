#!/usr/bin/env python3
"""Capture live firmware API metadata.

Requires the optional `websockets` Python package. This script is for developer
captures only; generated-doc checks do not import it.
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import hmac
import json
import sys
from typing import Any


async def call(
    ws: Any,
    method: str,
    params: dict[str, Any] | None = None,
    *,
    session_token: str | None = None,
    request_id: int,
) -> dict[str, Any]:
    message: dict[str, Any] = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
        "params": params or {},
    }
    if session_token is not None:
        message["session_token"] = session_token

    await ws.send(json.dumps(message, separators=(",", ":")))
    response = json.loads(await ws.recv())
    if "error" in response:
        raise RuntimeError(f"{method}: {response['error']}")
    return response


async def capture_tools_list(host: str, token: str, principal: str, include_device_id: bool) -> dict[str, Any]:
    try:
        import websockets
    except ImportError as exc:
        raise SystemExit("tools/capture_device_api.py requires: python -m pip install websockets") from exc

    async with websockets.connect(f"ws://{host}:8080/ws", open_timeout=5, ping_interval=None) as ws:
        begin = await call(ws, "local_control/auth_begin", request_id=1)
        challenge = begin["result"]
        proof_message = (
            f"{challenge['device_id']}:{challenge['challenge_id']}:{challenge['nonce']}:{principal}"
        )
        proof = hmac.new(token.encode(), proof_message.encode(), hashlib.sha256).hexdigest()
        verify = await call(
            ws,
            "local_control/auth_verify",
            {
                "challenge_id": challenge["challenge_id"],
                "principal": principal,
                "proof": proof,
            },
            request_id=2,
        )
        session_token = verify["result"]["session_token"]

        initialize = await call(ws, "initialize", {}, session_token=session_token, request_id=3)
        tools: list[dict[str, Any]] = []
        cursor = None
        request_id = 4
        while True:
            params: dict[str, Any] = {}
            if cursor is not None:
                params["cursor"] = cursor
            page = await call(ws, "tools/list", params, session_token=session_token, request_id=request_id)
            request_id += 1
            result = page.get("result") or {}
            tools.extend(result.get("tools") or [])
            cursor = result.get("nextCursor")
            if not cursor:
                break

    return {
        "source": "live-device-tools-list",
        "device_id": challenge["device_id"] if include_device_id else "<redacted>",
        "device_id_redacted": not include_device_id,
        "server_info": initialize["result"]["serverInfo"],
        "protocol_version": initialize["result"]["protocolVersion"],
        "tools": tools,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="device IP or hostname")
    parser.add_argument("--token", required=True, help="local control token")
    parser.add_argument("--principal", default="admin", choices=["admin", "dashboard", "mcp_bridge"])
    parser.add_argument("--include-device-id", action="store_true", help="include the real firmware device_id")
    args = parser.parse_args()

    payload = asyncio.run(capture_tools_list(args.host, args.token, args.principal, args.include_device_id))
    json.dump(payload, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
