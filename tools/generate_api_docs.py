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
VOICE_MESSAGES_PATH = REPO_ROOT / "docs/api/voice-websocket.messages.json"
LOCAL_CONTROL_EVENTS_PATH = REPO_ROOT / "docs/api/local-control.events.json"
LOCAL_CONTROL_SOURCE = REPO_ROOT / "firmware/main/hal/board/local_control_websocket_server.cc"
VOICE_HANDLER_SOURCE = REPO_ROOT / "firmware/main/runtime/protocol_message_handler_runtime.cc"
VOICE_OUTBOUND_SOURCE = REPO_ROOT / "firmware/main/runtime/protocol_runtime.cc"
LOCAL_CONTROL_DOC = REPO_ROOT / "docs/generated/local-control-websocket.md"
LOCAL_CONTROL_EVENTS_DOC = REPO_ROOT / "docs/generated/local-control-events.md"
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
        if message.get("direction") not in {"device_to_server", "server_to_device", "bidirectional"}:
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.direction is invalid")
        if not isinstance(message.get("summary"), str):
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.summary must be a string")
        if not isinstance(message.get("schema"), dict):
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.schema must be an object")
        if not isinstance(message.get("example"), dict):
            raise SystemExit(f"{VOICE_MESSAGES_PATH}: {name}.example must be an object")


def voice_message_contract_name(message_name: str) -> str:
    special_names = {
        "auth_begin": "auth",
        "auth_challenge": "auth",
        "auth_verify": "auth",
        "auth_ok": "auth",
        "device_hello": "hello",
        "server_hello": "hello",
    }
    return special_names.get(message_name, message_name)


def extract_voice_inbound_types(source: str) -> set[str]:
    return set(re.findall(r'std::strcmp\(type->valuestring,\s*"([^"]+)"\)', source))


def extract_voice_outbound_types(source: str) -> set[str]:
    return set(re.findall(r'cJSON_AddStringToObject\(root,\s*"type",\s*"([^"]+)"\)', source))


def check_voice_runtime_drift(spec: dict[str, Any]) -> None:
    handler_source = VOICE_HANDLER_SOURCE.read_text(encoding="utf-8")
    outbound_source = VOICE_OUTBOUND_SOURCE.read_text(encoding="utf-8")

    documented_messages = spec["messages"]
    documented_inbound = {
        voice_message_contract_name(message["name"])
        for message in documented_messages
        if message["direction"] in {"server_to_device", "bidirectional"}
    }
    documented_outbound = {
        voice_message_contract_name(message["name"])
        for message in documented_messages
        if message["direction"] in {"device_to_server", "bidirectional"}
    }

    handler_types = extract_voice_inbound_types(handler_source)
    outbound_types = extract_voice_outbound_types(outbound_source)

    missing_in_docs = sorted((handler_types - {"auth", "hello"}) - documented_inbound)
    missing_in_handler = sorted(documented_inbound - handler_types - {"auth", "hello"})
    missing_outbound_docs = sorted((outbound_types - {"abort"}) - documented_outbound)
    missing_outbound_code = sorted(documented_outbound - outbound_types - {"auth", "hello"})

    legacy_markers = {
        "\"type\", \"llm\"": handler_source,
        "\"type\", \"alert\"": handler_source,
        "\"type\", \"system\"": handler_source,
        "\"type\", \"custom\"": handler_source,
        "\"type\", \"tts\"": handler_source,
        "\"type\", \"stt\"": handler_source,
        "sentence_start": handler_source,
        "\"type\", \"listen\"": outbound_source,
        "\"type\", \"mcp\"": outbound_source,
    }
    legacy_hits = sorted(marker for marker, body in legacy_markers.items() if marker in body)

    if missing_in_docs or missing_in_handler or missing_outbound_docs or missing_outbound_code or legacy_hits:
        lines = ["voice WebSocket contract drift detected:"]
        if missing_in_docs:
            lines.append("  inbound handler messages missing from voice metadata:")
            lines.extend(f"    - {name}" for name in missing_in_docs)
        if missing_in_handler:
            lines.append("  documented inbound messages missing from handler code:")
            lines.extend(f"    - {name}" for name in missing_in_handler)
        if missing_outbound_docs:
            lines.append("  outbound protocol messages missing from voice metadata:")
            lines.extend(f"    - {name}" for name in missing_outbound_docs)
        if missing_outbound_code:
            lines.append("  documented outbound messages missing from protocol code:")
            lines.extend(f"    - {name}" for name in missing_outbound_code)
        if legacy_hits:
            lines.append("  legacy voice message markers still present:")
            lines.extend(f"    - {marker}" for marker in legacy_hits)
        raise SystemExit("\n".join(lines))


def validate_local_control_events(spec: dict[str, Any]) -> None:
    events = spec.get("events")
    if not isinstance(events, list):
        raise SystemExit(f"{LOCAL_CONTROL_EVENTS_PATH}: events must be an array")
    seen: set[str] = set()
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            raise SystemExit(f"{LOCAL_CONTROL_EVENTS_PATH}: events[{index}] must be an object")
        name = event.get("name")
        if not isinstance(name, str) or not name:
            raise SystemExit(f"{LOCAL_CONTROL_EVENTS_PATH}: events[{index}].name must be a non-empty string")
        if name in seen:
            raise SystemExit(f"{LOCAL_CONTROL_EVENTS_PATH}: duplicate event {name}")
        seen.add(name)
        if not isinstance(event.get("summary"), str):
            raise SystemExit(f"{LOCAL_CONTROL_EVENTS_PATH}: {name}.summary must be a string")
        if not isinstance(event.get("schema"), dict):
            raise SystemExit(f"{LOCAL_CONTROL_EVENTS_PATH}: {name}.schema must be an object")
        if not isinstance(event.get("example"), dict):
            raise SystemExit(f"{LOCAL_CONTROL_EVENTS_PATH}: {name}.example must be an object")


def render_local_control_events_doc(spec: dict[str, Any]) -> str:
    validate_local_control_events(spec)
    lines = [
        "# Local Control Debug Events",
        "",
        "Generated by `tools/generate_api_docs.py`. Do not edit by hand.",
        "",
        "These notifications are delivered on the authenticated local-control",
        "WebSocket after `debug/subscribe_events` succeeds.",
        "",
        "Envelope:",
        "",
        "```json",
        pretty_json(
            {
                "jsonrpc": "2.0",
                "method": "debug/event",
                "params": {
                    "ts_ms": 123456789,
                    "type": "listen_enter",
                    "fields": {"mode": "auto"},
                },
            }
        ),
        "```",
        "",
        "## Event Index",
        "",
    ]
    for event in spec["events"]:
        lines.append(f"- `{event['name']}`: {event['summary']}")

    lines.append("")
    for event in spec["events"]:
        lines.extend(
            [
                f"## {event['name']}",
                "",
                event["summary"],
                "",
                "Example:",
                "",
                "```json",
                pretty_json(event["example"]),
                "```",
                "",
                "Schema:",
                "",
                "```json",
                pretty_json(event["schema"]),
                "```",
                "",
            ]
        )

    return "\n".join(lines).rstrip() + "\n"


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


def render_api_index(spec: dict[str, Any], voice_spec: dict[str, Any],
                     local_control_events_spec: dict[str, Any]) -> str:
    payload = {
        "version": spec["version"],
        "generated": {
            "local_control_events_markdown": str(LOCAL_CONTROL_EVENTS_DOC.relative_to(REPO_ROOT)),
            "local_control_events_metadata": str(LOCAL_CONTROL_EVENTS_PATH.relative_to(REPO_ROOT)),
            "local_control_markdown": str(LOCAL_CONTROL_DOC.relative_to(REPO_ROOT)),
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
        "local_control_events": [
            {
                "name": event["name"],
                "summary": event["summary"],
                "schema": event["schema"],
            }
            for event in local_control_events_spec["events"]
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
    voice_spec = load_json(VOICE_MESSAGES_PATH)
    local_control_events_spec = load_json(LOCAL_CONTROL_EVENTS_PATH)
    check_source_drift(spec)
    validate_voice_spec(voice_spec)
    check_voice_runtime_drift(voice_spec)
    validate_local_control_events(local_control_events_spec)

    local_doc = render_local_control_doc(spec)
    local_control_events_doc = render_local_control_events_doc(local_control_events_spec)
    voice_doc = render_voice_websocket_doc(voice_spec)
    api_index = render_api_index(spec, voice_spec, local_control_events_spec)

    ok = True
    ok = write_or_check(LOCAL_CONTROL_DOC, local_doc, args.check) and ok
    ok = write_or_check(LOCAL_CONTROL_EVENTS_DOC, local_control_events_doc, args.check) and ok
    ok = write_or_check(VOICE_WEBSOCKET_DOC, voice_doc, args.check) and ok
    ok = write_or_check(API_INDEX, api_index, args.check) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
