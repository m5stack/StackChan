#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import hashlib
import hmac
import json
import sys
from pathlib import Path
from typing import Any

import websockets


def build_parser() -> argparse.ArgumentParser:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Smoke-test the StackChan device API from generated metadata")
    parser.add_argument("--device-ip", required=True, help="Device IP, e.g. 192.168.68.62")
    parser.add_argument("--token", required=True, help="Local control token")
    parser.add_argument("--principal", default="admin", help="Auth principal to use")
    parser.add_argument(
        "--api-index",
        default=str(repo_root / "docs/generated/api-index.json"),
        help="Generated API index JSON",
    )
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=15.0,
        help="Per-request receive timeout",
    )
    parser.add_argument(
        "--allow-side-effects",
        action="store_true",
        help="Include non-read-only local methods such as voice/start_listening",
    )
    parser.add_argument(
        "--skip-negative-tests",
        action="store_true",
        help="Skip unauthenticated/invalid-request negative coverage",
    )
    return parser


def load_api_index(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def build_auth_proof(token: str, device_id: str, challenge_id: str, nonce: str, principal: str) -> str:
    message = f"{device_id}:{challenge_id}:{nonce}:{principal}".encode()
    return hmac.new(token.encode(), message, hashlib.sha256).hexdigest()


def schema_type_matches(expected_type: str, value: Any) -> bool:
    if expected_type == "object":
        return isinstance(value, dict)
    if expected_type == "array":
        return isinstance(value, list)
    if expected_type == "string":
        return isinstance(value, str)
    if expected_type == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected_type == "boolean":
        return isinstance(value, bool)
    if expected_type == "number":
        return (isinstance(value, int) or isinstance(value, float)) and not isinstance(value, bool)
    return True


def validate_schema(value: Any, schema: dict[str, Any], path: str = "result") -> list[str]:
    # Intentionally partial JSON Schema validation for firmware contract smoke tests.
    # We enforce core structural guarantees here and leave full schema coverage to
    # future host-side validation if needed.
    errors: list[str] = []
    expected_type = schema.get("type")
    if isinstance(expected_type, str) and not schema_type_matches(expected_type, value):
        errors.append(f"{path}: expected {expected_type}, got {type(value).__name__}")
        return errors

    if "const" in schema and value != schema["const"]:
        errors.append(f"{path}: expected const {schema['const']!r}, got {value!r}")

    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: expected one of {schema['enum']!r}, got {value!r}")

    if isinstance(value, dict):
        required = schema.get("required", [])
        for key in required:
            if key not in value:
                errors.append(f"{path}: missing required key {key!r}")
        properties = schema.get("properties", {})
        for key, subschema in properties.items():
            if key in value and isinstance(subschema, dict):
                errors.extend(validate_schema(value[key], subschema, f"{path}.{key}"))
    elif isinstance(value, list):
        item_schema = schema.get("items")
        if isinstance(item_schema, dict):
            for index, item in enumerate(value):
                errors.extend(validate_schema(item, item_schema, f"{path}[{index}]"))
    return errors


def is_read_only_local_method(method_name: str) -> bool:
    if method_name in {"local_control/auth_begin", "local_control/auth_verify"}:
        return True
    tail = method_name.rsplit("/", 1)[-1].rsplit(".", 1)[-1]
    return tail == "get" or tail.startswith("get_")


def synthesize_value(schema: dict[str, Any]) -> Any:
    if "const" in schema:
        return schema["const"]
    if "default" in schema:
        return schema["default"]
    if "enum" in schema:
        return schema["enum"][0]

    expected_type = schema.get("type")
    if expected_type == "string":
        pattern = schema.get("pattern")
        if pattern == "^wss?://":
            return "ws://example.invalid/ws"
        min_length = max(int(schema.get("minLength", 1)), 1)
        return "x" * min_length
    if expected_type == "integer":
        if "minimum" in schema:
            return int(schema["minimum"])
        return 0
    if expected_type == "boolean":
        return False
    if expected_type == "array":
        item_schema = schema.get("items", {})
        if isinstance(item_schema, dict):
            return [synthesize_value(item_schema)]
        return []
    if expected_type == "object":
        properties = schema.get("properties", {})
        required = schema.get("required", [])
        result: dict[str, Any] = {}
        for key in required:
            subschema = properties.get(key, {})
            if isinstance(subschema, dict):
                result[key] = synthesize_value(subschema)
        return result
    return None


def synthesize_invalid_value(schema: dict[str, Any]) -> Any:
    expected_type = schema.get("type")
    if expected_type == "string":
        return 123
    if expected_type == "integer":
        return "not-an-integer"
    if expected_type == "boolean":
        return "not-a-boolean"
    if expected_type == "array":
        return {}
    if expected_type == "object":
        return []
    return None


def build_valid_params(schema: dict[str, Any]) -> dict[str, Any]:
    properties = schema.get("properties", {})
    required = schema.get("required", [])
    params: dict[str, Any] = {}
    for key in required:
        subschema = properties.get(key, {})
        if isinstance(subschema, dict):
            params[key] = synthesize_value(subschema)
    return params


def build_missing_required_variants(schema: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    properties = schema.get("properties", {})
    required = schema.get("required", [])
    valid_params = build_valid_params(schema)
    variants: list[tuple[str, dict[str, Any]]] = []
    for key in required:
        params = dict(valid_params)
        params.pop(key, None)
        variants.append((key, params))
    return variants


def build_wrong_type_variants(schema: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    properties = schema.get("properties", {})
    required = schema.get("required", [])
    valid_params = build_valid_params(schema)
    variants: list[tuple[str, dict[str, Any]]] = []
    for key in required:
        subschema = properties.get(key, {})
        if not isinstance(subschema, dict):
            continue
        invalid_value = synthesize_invalid_value(subschema)
        if invalid_value is None:
            continue
        params = dict(valid_params)
        params[key] = invalid_value
        variants.append((key, params))
    return variants


async def rpc(
    ws: websockets.ClientConnection,
    method: str,
    params: dict[str, Any] | None,
    req_id: int,
    timeout_seconds: float,
    session_token: str | None = None,
) -> dict[str, Any]:
    # The transport still gates the initial WebSocket upgrade with the persisted
    # local-control token. The HMAC challenge then binds the session principal and
    # upgrades the connection to an authenticated session token.
    message: dict[str, Any] = {"jsonrpc": "2.0", "id": req_id, "type": "rpc", "method": method}
    if params is not None:
        message["params"] = params
    if session_token is not None:
        message["session_token"] = session_token
    await ws.send(json.dumps(message))
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=timeout_seconds)
        if isinstance(raw, bytes):
            continue
        data = json.loads(raw)
        if data.get("id") == req_id:
            return data


def require_error(response: dict[str, Any], expected_code: int, label: str) -> None:
    error = response.get("error")
    if not isinstance(error, dict):
        raise RuntimeError(f"{label}: expected error response, got {json.dumps(response)}")
    if error.get("code") != expected_code:
        raise RuntimeError(
            f"{label}: expected error code {expected_code}, got {error.get('code')}: {json.dumps(response)}"
        )


async def authenticate(
    ws: websockets.ClientConnection,
    token: str,
    principal: str,
    timeout_seconds: float,
) -> tuple[str, dict[str, Any]]:
    begin = await rpc(ws, "local_control/auth_begin", {"principal": principal}, 1, timeout_seconds)
    begin_result = begin["result"]
    proof = build_auth_proof(
        token,
        begin_result["device_id"],
        begin_result["challenge_id"],
        begin_result["nonce"],
        principal,
    )
    verify = await rpc(
        ws,
        "local_control/auth_verify",
        {
            "challenge_id": begin_result["challenge_id"],
            "principal": principal,
            "proof": proof,
        },
        2,
        timeout_seconds,
    )
    return verify["result"]["session_token"], verify


async def run_negative_tests(
    ws: websockets.ClientConnection,
    api_index: dict[str, Any],
    timeout_seconds: float,
    summary: list[str],
) -> None:
    req_id = 200
    unauth_methods = [
        ("initialize", {}),
        ("tools/list", {}),
        ("settings/get", {}),
        ("voice/get_config", {}),
        ("voice.get_config", {}),
    ]
    for method, params in unauth_methods:
        response = await rpc(ws, method, params, req_id, timeout_seconds)
        require_error(response, -32001, f"{method} unauthenticated")
        summary.append(f"{method} unauth rejected")
        req_id += 1

    local_methods = api_index.get("local_control_methods", [])
    for method_spec in local_methods:
        method_name = method_spec["name"]
        if method_name == "local_control/auth_begin":
            continue
        schema = method_spec.get("params_schema", {"type": "object"})
        if method_name == "local_control/auth_verify":
            missing_variants = build_missing_required_variants(schema)
            wrong_type_variants = build_wrong_type_variants(schema)
            for key, params in missing_variants:
                response = await rpc(ws, method_name, params, req_id, timeout_seconds)
                require_error(response, -32602, f"{method_name} missing {key}")
                summary.append(f"{method_name} missing {key} rejected")
                req_id += 1
            for key, params in wrong_type_variants:
                response = await rpc(ws, method_name, params, req_id, timeout_seconds)
                require_error(response, -32602, f"{method_name} wrong type {key}")
                summary.append(f"{method_name} wrong type {key} rejected")
                req_id += 1
            continue

        if not method_spec.get("auth", {}).get("required", False):
            continue

        response = await rpc(ws, method_name, {}, req_id, timeout_seconds, session_token="bad-session-token")
        require_error(response, -32001, f"{method_name} bad session")
        summary.append(f"{method_name} bad session rejected")
        req_id += 1

        for key, params in build_missing_required_variants(schema):
            response = await rpc(ws, method_name, params, req_id, timeout_seconds, session_token="bad-session-token")
            require_error(response, -32001, f"{method_name} unauth missing {key}")
            summary.append(f"{method_name} unauth missing {key} rejected")
            req_id += 1


async def run_negative_tests_authenticated(
    ws: websockets.ClientConnection,
    api_index: dict[str, Any],
    timeout_seconds: float,
    session_token: str,
    summary: list[str],
) -> None:
    req_id = 400
    local_methods = api_index.get("local_control_methods", [])
    for method_spec in local_methods:
        method_name = method_spec["name"]
        if method_name in {"local_control/auth_begin", "local_control/auth_verify"}:
            continue
        schema = method_spec.get("params_schema", {"type": "object"})

        for key, params in build_missing_required_variants(schema):
            response = await rpc(ws, method_name, params, req_id, timeout_seconds, session_token=session_token)
            require_error(response, -32602, f"{method_name} missing {key}")
            summary.append(f"{method_name} missing {key} rejected")
            req_id += 1

        for key, params in build_wrong_type_variants(schema):
            response = await rpc(ws, method_name, params, req_id, timeout_seconds, session_token=session_token)
            require_error(response, -32602, f"{method_name} wrong type {key}")
            summary.append(f"{method_name} wrong type {key} rejected")
            req_id += 1


async def run_smoke_test(args: argparse.Namespace) -> int:
    api_index = load_api_index(Path(args.api_index))
    url = f"ws://{args.device_ip}:8080/ws?token={args.token}"
    summary: list[str] = []

    async with websockets.connect(url, open_timeout=args.timeout_seconds, ping_interval=20) as ws:
        if not args.skip_negative_tests:
            await run_negative_tests(ws, api_index, args.timeout_seconds, summary)

        session_token, verify = await authenticate(ws, args.token, args.principal, args.timeout_seconds)

        verify_errors = validate_schema(
            verify["result"],
            next(item["result_schema"] for item in api_index["local_control_methods"] if item["name"] == "local_control/auth_verify"),
        )
        if verify_errors:
            print("\n".join(verify_errors), file=sys.stderr)
            return 1
        summary.append("auth ok")

        initialize = await rpc(ws, "initialize", {}, 10, args.timeout_seconds, session_token=session_token)
        if "error" in initialize:
            print(json.dumps(initialize, indent=2))
            return 1
        if initialize.get("result", {}).get("protocolVersion") != "2024-11-05":
            print("initialize.protocolVersion mismatch", file=sys.stderr)
            return 1
        summary.append("initialize ok")

        tools_list = await rpc(ws, "tools/list", {}, 11, args.timeout_seconds, session_token=session_token)
        if "error" in tools_list:
            print(json.dumps(tools_list, indent=2))
            return 1
        live_tool_names = {tool["name"] for tool in tools_list.get("result", {}).get("tools", [])}
        expected_tool_names = {tool["name"] for tool in api_index.get("mcp_tools", [])}
        missing_tools = sorted(expected_tool_names - live_tool_names)
        if missing_tools:
            print(f"tools/list missing expected tools: {missing_tools}", file=sys.stderr)
            return 1
        summary.append(f"tools/list ok ({len(live_tool_names)} tools)")

        local_methods = api_index.get("local_control_methods", [])
        req_id = 20
        for method_spec in local_methods:
            method_name = method_spec["name"]
            if method_name in {"local_control/auth_begin", "local_control/auth_verify"}:
                continue
            if not args.allow_side_effects and not is_read_only_local_method(method_name):
                continue

            response = await rpc(ws, method_name, {}, req_id, args.timeout_seconds, session_token=session_token)
            req_id += 1
            if "error" in response:
                print(json.dumps(response, indent=2))
                return 1
            result_schema = method_spec.get("result_schema", {"type": "object"})
            errors = validate_schema(response.get("result"), result_schema)
            if errors:
                print(f"{method_name} schema validation failed:", file=sys.stderr)
                print("\n".join(errors), file=sys.stderr)
                return 1
            summary.append(f"{method_name} ok")
            for alias in method_spec.get("aliases", []):
                alias_response = await rpc(ws, alias, {}, req_id, args.timeout_seconds, session_token=session_token)
                req_id += 1
                if "error" in alias_response:
                    print(json.dumps(alias_response, indent=2))
                    return 1
                alias_errors = validate_schema(alias_response.get("result"), result_schema)
                if alias_errors:
                    print(f"{alias} schema validation failed:", file=sys.stderr)
                    print("\n".join(alias_errors), file=sys.stderr)
                    return 1
                summary.append(f"{alias} ok")

        if not args.skip_negative_tests:
            await run_negative_tests_authenticated(ws, api_index, args.timeout_seconds, session_token, summary)

    print("\n".join(summary))
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return asyncio.run(run_smoke_test(args))


if __name__ == "__main__":
    raise SystemExit(main())
