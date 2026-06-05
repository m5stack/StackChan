#!/usr/bin/env python3
"""Generate and check API docs from repo-side metadata.

This intentionally keeps API descriptions out of ESP32 firmware. The script
checks metadata against firmware source strings, then renders deterministic
Markdown docs for humans.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
METHODS_PATH = REPO_ROOT / "docs/api/local-control.methods.json"
MCP_TOOLS_FIXTURE = REPO_ROOT / "docs/api/fixtures/tools-list.stackchan.json"
VOICE_MESSAGES_PATH = REPO_ROOT / "docs/api/voice-websocket.messages.json"
LOCAL_CONTROL_SOURCE = REPO_ROOT / "firmware/main/hal/board/local_control_websocket_server.cc"
LOCAL_CONTROL_DOC = REPO_ROOT / "docs/generated/local-control-websocket.md"
MCP_TOOLS_DOC = REPO_ROOT / "docs/generated/mcp-tools.md"
VOICE_WEBSOCKET_DOC = REPO_ROOT / "docs/generated/voice-websocket.md"
API_INDEX = REPO_ROOT / "docs/generated/api-index.json"


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"{path}: expected a JSON object")
    return data


def extract_function_body(source: str, function_name: str) -> str:
    marker = source.find(function_name)
    if marker < 0:
        raise SystemExit(f"{LOCAL_CONTROL_SOURCE}: missing {function_name}")

    brace = source.find("{", marker)
    if brace < 0:
        raise SystemExit(f"{LOCAL_CONTROL_SOURCE}: missing body for {function_name}")

    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]

    raise SystemExit(f"{LOCAL_CONTROL_SOURCE}: unterminated body for {function_name}")


def extract_method_literals(body: str) -> set[str]:
    return set(re.findall(r'std::strcmp\(\s*method_name\s*,\s*"([^"]+)"\s*\)', body))


def extract_source_methods() -> set[str]:
    source = LOCAL_CONTROL_SOURCE.read_text(encoding="utf-8")
    local_body = extract_function_body(source, "IsLocalControlMethod")
    auth_body = extract_function_body(source, "LocalControlWebSocketServer::HandleAuthJsonRpc")

    methods = extract_method_literals(local_body)
    methods.update(
        method
        for method in extract_method_literals(auth_body)
        if method.startswith("local_control/auth_")
    )
    return methods


def metadata_methods(spec: dict[str, Any]) -> set[str]:
    methods = spec.get("methods")
    if not isinstance(methods, list):
        raise SystemExit(f"{METHODS_PATH}: methods must be an array")

    names: set[str] = set()
    for index, method in enumerate(methods):
        if not isinstance(method, dict):
            raise SystemExit(f"{METHODS_PATH}: methods[{index}] must be an object")
        name = method.get("name")
        if not isinstance(name, str) or not name:
            raise SystemExit(f"{METHODS_PATH}: methods[{index}].name must be a non-empty string")
        if name in names:
            raise SystemExit(f"{METHODS_PATH}: duplicate method {name}")
        names.add(name)

        aliases = method.get("aliases", [])
        if not isinstance(aliases, list) or not all(isinstance(alias, str) for alias in aliases):
            raise SystemExit(f"{METHODS_PATH}: {name}.aliases must be an array of strings")
        for alias in aliases:
            if alias in names:
                raise SystemExit(f"{METHODS_PATH}: duplicate method/alias {alias}")
            names.add(alias)

        auth = method.get("auth")
        if not isinstance(auth, dict) or "required" not in auth:
            raise SystemExit(f"{METHODS_PATH}: {name}.auth.required is required")

        for schema_key in ("params_schema", "result_schema"):
            schema = method.get(schema_key)
            if not isinstance(schema, dict):
                raise SystemExit(f"{METHODS_PATH}: {name}.{schema_key} must be an object")

    return names


def check_source_drift(spec: dict[str, Any]) -> None:
    documented = metadata_methods(spec)
    source = extract_source_methods()

    missing_in_metadata = sorted(source - documented)
    missing_in_source = sorted(documented - source)

    if missing_in_metadata or missing_in_source:
        lines = ["local-control API metadata drift detected:"]
        if missing_in_metadata:
            lines.append("  firmware methods missing from metadata:")
            lines.extend(f"    - {method}" for method in missing_in_metadata)
        if missing_in_source:
            lines.append("  metadata methods missing from firmware:")
            lines.extend(f"    - {method}" for method in missing_in_source)
        raise SystemExit("\n".join(lines))


def compact_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def pretty_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True)


def render_method(method: dict[str, Any]) -> str:
    aliases = method.get("aliases", [])
    auth = method["auth"]
    principals = auth.get("principals", [])
    auth_text = "not required" if not auth.get("required") else ", ".join(principals)

    lines = [
        f"## {method['name']}",
        "",
        method["summary"],
        "",
        f"- Stability: `{method.get('stability', 'unknown')}`",
        f"- Auth: `{auth_text}`",
    ]
    if aliases:
        lines.append(f"- Aliases: {', '.join(f'`{alias}`' for alias in aliases)}")
    if method.get("handler"):
        lines.append(f"- Handler: `{method['handler']}`")

    lines.extend(
        [
            "",
            "Params schema:",
            "",
            "```json",
            pretty_json(method["params_schema"]),
            "```",
            "",
            "Result schema:",
            "",
            "```json",
            pretty_json(method["result_schema"]),
            "```",
            "",
        ]
    )
    return "\n".join(lines)


def render_local_control_doc(spec: dict[str, Any]) -> str:
    methods = spec["methods"]
    lines = [
        "# Local Control WebSocket API",
        "",
        "Generated by `tools/generate_api_docs.py`. Do not edit by hand.",
        "",
        f"- Endpoint: `{spec['endpoint']}`",
        f"- Session token location: `{spec['session_token_location']}`",
        f"- Auth proof: `{spec['auth_proof']}`",
        "",
        "Principals:",
        "",
    ]
    lines.extend(f"- `{principal}`" for principal in spec.get("principals", []))
    lines.extend(
        [
            "",
            "## Method Index",
            "",
        ]
    )
    for method in methods:
        aliases = method.get("aliases", [])
        suffix = f" ({', '.join(aliases)})" if aliases else ""
        lines.append(f"- `{method['name']}`{suffix}: {method['summary']}")

    lines.append("")
    lines.extend(render_method(method) for method in methods)
    return "\n".join(lines).rstrip() + "\n"


def validate_mcp_fixture(fixture: dict[str, Any]) -> None:
    tools = fixture.get("tools")
    if not isinstance(tools, list):
        raise SystemExit(f"{MCP_TOOLS_FIXTURE}: tools must be an array")

    seen: set[str] = set()
    for index, tool in enumerate(tools):
        if not isinstance(tool, dict):
            raise SystemExit(f"{MCP_TOOLS_FIXTURE}: tools[{index}] must be an object")
        name = tool.get("name")
        if not isinstance(name, str) or not name:
            raise SystemExit(f"{MCP_TOOLS_FIXTURE}: tools[{index}].name must be a non-empty string")
        if name in seen:
            raise SystemExit(f"{MCP_TOOLS_FIXTURE}: duplicate tool {name}")
        seen.add(name)
        if not isinstance(tool.get("description"), str):
            raise SystemExit(f"{MCP_TOOLS_FIXTURE}: {name}.description must be a string")
        schema = tool.get("inputSchema")
        if not isinstance(schema, dict):
            raise SystemExit(f"{MCP_TOOLS_FIXTURE}: {name}.inputSchema must be an object")


def schema_properties_summary(schema: dict[str, Any]) -> list[str]:
    properties = schema.get("properties") or {}
    if not isinstance(properties, dict) or not properties:
        return ["- Params: none"]

    required = schema.get("required") or []
    required_set = set(required if isinstance(required, list) else [])
    lines = ["- Params:"]
    for name, prop in properties.items():
        if not isinstance(prop, dict):
            continue
        pieces = [f"`{name}`"]
        prop_type = prop.get("type")
        if isinstance(prop_type, str):
            pieces.append(prop_type)
        if "minimum" in prop or "maximum" in prop:
            pieces.append(f"range `{prop.get('minimum', '?')}`..`{prop.get('maximum', '?')}`")
        if "default" in prop:
            pieces.append(f"default `{prop['default']}`")
        if name in required_set:
            pieces.append("required")
        lines.append(f"  - {', '.join(pieces)}")
    return lines


def render_mcp_tools_doc(fixture: dict[str, Any]) -> str:
    validate_mcp_fixture(fixture)
    server_info = fixture.get("server_info") or {}
    tools = fixture["tools"]

    lines = [
        "# MCP Tools",
        "",
        "Generated by `tools/generate_api_docs.py` from `docs/api/fixtures/tools-list.stackchan.json`.",
        "Do not edit by hand.",
        "",
        f"- Source: `{fixture.get('source', 'unknown')}`",
        f"- Device ID: `{fixture.get('device_id', 'unknown')}`",
        f"- Protocol version: `{fixture.get('protocol_version', 'unknown')}`",
        f"- Server: `{server_info.get('name', 'unknown')}` `{server_info.get('version', 'unknown')}`",
        "",
        "These tools come from the vendored Xiaozhi MCP server exposed through",
        "authenticated local control routing. They are not local-control RPC",
        "methods.",
        "",
        "## Tool Index",
        "",
    ]

    for tool in tools:
        lines.append(f"- `{tool['name']}`")

    lines.append("")
    for tool in tools:
        lines.extend(
            [
                f"## {tool['name']}",
                "",
                tool["description"],
                "",
            ]
        )
        lines.extend(schema_properties_summary(tool["inputSchema"]))
        lines.extend(
            [
                "",
                "Input schema:",
                "",
                "```json",
                pretty_json(tool["inputSchema"]),
                "```",
                "",
            ]
        )

    return "\n".join(lines).rstrip() + "\n"


def validate_voice_spec(spec: dict[str, Any]) -> None:
    messages = spec.get("messages")
    if not isinstance(messages, list):
        raise SystemExit(f"{VOICE_MESSAGES_PATH}: messages must be an array")
    seen: set[str] = set()
    for index, message in enumerate(messages):
        if not isinstance(message, dict):
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: messages[{index}] must be an object")
        name = message.get("name")
        if not isinstance(name, str) or not name:
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: messages[{index}].name must be a non-empty string")
        if name in seen:
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: duplicate message {name}")
        seen.add(name)
        if message.get("direction") not in {"device_to_server", "server_to_device"}:
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.direction is invalid")
        if not isinstance(message.get("summary"), str):
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.summary must be a string")
        if not isinstance(message.get("schema"), dict):
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.schema must be an object")
        if not isinstance(message.get("example"), dict):
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.example must be an object")


def render_voice_websocket_doc(spec: dict[str, Any]) -> str:
    validate_voice_spec(spec)
    auth = spec["auth"]
    lines = [
        "# Voice WebSocket",
        "",
        "Generated by `tools/generate_api_docs.py`. Do not edit by hand.",
        "",
        f"- Endpoint: `{spec['endpoint']}`",
        f"- Transport: `{spec['transport']}`",
        f"- Auth algorithm: `{auth['algorithm']}`",
        f"- Server proof message: `{auth['server_proof_message']}`",
        f"- Device proof message: `{auth['device_proof_message']}`",
        "",
        "The device opens this WebSocket after local wake-word detection or an",
        "explicit voice start request. This is not the permanent robot control",
        "channel; local-control RPC remains the source of truth for settings,",
        "pairing, and durable robot control.",
        "",
        "## Message Index",
        "",
    ]
    for message in spec["messages"]:
        lines.append(f"- `{message['name']}`: {message['summary']}")

    lines.extend(["", "## Binary Frames", ""])
    for frame in spec.get("binary_frames", []):
        lines.append(f"- `{frame['direction']}`: {frame['description']}")

    lines.append("")
    for message in spec["messages"]:
        lines.extend(
            [
                f"## {message['name']}",
                "",
                message["summary"],
                "",
                f"- Direction: `{message['direction']}`",
                "",
                "Example:",
                "",
                "```json",
                pretty_json(message["example"]),
                "```",
                "",
                "Schema:",
                "",
                "```json",
                pretty_json(message["schema"]),
                "```",
                "",
            ]
        )

    return "\n".join(lines).rstrip() + "\n"


def render_api_index(spec: dict[str, Any], mcp_fixture: dict[str, Any], voice_spec: dict[str, Any]) -> str:
    payload = {
        "version": spec["version"],
        "generated": {
            "local_control_markdown": str(LOCAL_CONTROL_DOC.relative_to(REPO_ROOT)),
            "mcp_tools_markdown": str(MCP_TOOLS_DOC.relative_to(REPO_ROOT)),
            "mcp_tools_fixture": str(MCP_TOOLS_FIXTURE.relative_to(REPO_ROOT)),
            "source_metadata": str(METHODS_PATH.relative_to(REPO_ROOT)),
            "voice_messages_metadata": str(VOICE_MESSAGES_PATH.relative_to(REPO_ROOT)),
            "voice_websocket_markdown": str(VOICE_WEBSOCKET_DOC.relative_to(REPO_ROOT)),
        },
        "local_control_methods": [
            {
                "name": method["name"],
                "aliases": method.get("aliases", []),
                "stability": method.get("stability", "unknown"),
                "auth": method["auth"],
                "params_schema": method["params_schema"],
                "result_schema": method["result_schema"],
            }
            for method in spec["methods"]
        ],
        "mcp_tools": [
            {
                "name": tool["name"],
                "description": tool["description"],
                "inputSchema": tool["inputSchema"],
            }
            for tool in mcp_fixture["tools"]
        ],
        "voice_messages": [
            {
                "name": message["name"],
                "direction": message["direction"],
                "summary": message["summary"],
                "schema": message["schema"],
            }
            for message in voice_spec["messages"]
        ],
    }
    return pretty_json(payload) + "\n"


def write_or_check(path: Path, content: str, check: bool) -> bool:
    if check:
        existing = path.read_text(encoding="utf-8") if path.exists() else None
        if existing != content:
            print(f"{path.relative_to(REPO_ROOT)} is stale; run tools/generate_api_docs.py", file=sys.stderr)
            return False
        return True

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(f"wrote {path.relative_to(REPO_ROOT)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if generated docs are stale")
    args = parser.parse_args()

    spec = load_json(METHODS_PATH)
    mcp_fixture = load_json(MCP_TOOLS_FIXTURE)
    voice_spec = load_json(VOICE_MESSAGES_PATH)
    check_source_drift(spec)
    validate_mcp_fixture(mcp_fixture)
    validate_voice_spec(voice_spec)

    local_doc = render_local_control_doc(spec)
    mcp_doc = render_mcp_tools_doc(mcp_fixture)
    voice_doc = render_voice_websocket_doc(voice_spec)
    api_index = render_api_index(spec, mcp_fixture, voice_spec)

    ok = True
    ok = write_or_check(LOCAL_CONTROL_DOC, local_doc, args.check) and ok
    ok = write_or_check(MCP_TOOLS_DOC, mcp_doc, args.check) and ok
    ok = write_or_check(VOICE_WEBSOCKET_DOC, voice_doc, args.check) and ok
    ok = write_or_check(API_INDEX, api_index, args.check) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
