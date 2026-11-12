"""Tests for Stack-chan event notification dispatch modes."""

from pathlib import Path

import pytest

from stackchan_mcp import esp32_client, event_log, stdio_server
from stackchan_mcp.esp32_client import ESP32Manager
from stackchan_mcp.http_server import build_app
from stackchan_mcp.notify_config import (
    DEFAULT_MESSAGE_TEMPLATES,
    MessageTemplate,
    NotifyConfig,
)
from stackchan_mcp.queue import CommandQueue


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("legacy_enabled", "channels_enabled", "jsonl_enabled"),
    [
        (False, False, False),
        (True, False, False),
        (False, True, False),
        (False, False, True),
        (True, True, False),
        (True, False, True),
        (False, True, True),
        (True, True, True),
    ],
)
async def test_emit_stackchan_event_dispatches_selected_paths(
    monkeypatch,
    caplog,
    legacy_enabled,
    channels_enabled,
    jsonl_enabled,
):
    notify_calls: list[tuple[str, dict]] = []
    log_calls: list[dict] = []

    async def fake_notify(method, params):
        notify_calls.append((method, params))

    def fake_log_event(**kwargs):
        log_calls.append(kwargs)

    monkeypatch.setattr(stdio_server, "notify_stackchan_event", fake_notify)
    monkeypatch.setattr(event_log, "log_event", fake_log_event)
    monkeypatch.setattr(esp32_client.time, "time", lambda: 1717000000.25)

    jsonl_path = Path("/tmp/stackchan-events-test.jsonl")
    manager = ESP32Manager(
        notify_config=_notify_config(
            legacy=legacy_enabled,
            channels=channels_enabled,
            jsonl=jsonl_enabled,
            jsonl_path=jsonl_path,
        )
    )

    with caplog.at_level("INFO"):
        await manager._emit_stackchan_event(_payload())

    expected_notify_methods = []
    if legacy_enabled:
        expected_notify_methods.append(stdio_server.STACKCHAN_EVENT_METHOD)
    if channels_enabled:
        expected_notify_methods.append(stdio_server.CHANNEL_NOTIFICATION_METHOD)
    assert [method for method, _ in notify_calls] == expected_notify_methods

    if legacy_enabled:
        legacy_params = dict(notify_calls[0][1])
        assert legacy_params == _expected_legacy_params()
        assert legacy_params["action"] == "head_pat"

    if channels_enabled:
        channel_index = expected_notify_methods.index(stdio_server.CHANNEL_NOTIFICATION_METHOD)
        channel_params = notify_calls[channel_index][1]
        assert channel_params == {
            "content": DEFAULT_MESSAGE_TEMPLATES[("touch", "tap")].template,
            "meta": _expected_meta(),
        }
        assert channel_params["meta"]["action"] == "head_pat"

    if jsonl_enabled:
        assert log_calls == [
            {
                "event_type": "touch",
                "subtype": "tap",
                "duration_ms": 350,
                "ts": 123456,
                "session_id": "session-1",
                "action": "head_pat",
                "path": jsonl_path,
                "ts_unix": 1717000000.25,
            }
        ]
    else:
        assert log_calls == []

    if not (legacy_enabled or channels_enabled or jsonl_enabled):
        assert notify_calls == []
        assert "received and dropped" in caplog.text


@pytest.mark.asyncio
async def test_custom_message_overrides_action_and_channel_content(monkeypatch):
    notify_calls: list[tuple[str, dict]] = []

    async def fake_notify(method, params):
        notify_calls.append((method, params))

    monkeypatch.setattr(stdio_server, "notify_stackchan_event", fake_notify)
    monkeypatch.setattr(esp32_client.time, "time", lambda: 1717000000.25)

    messages = dict(DEFAULT_MESSAGE_TEMPLATES)
    messages[("touch", "tap")] = MessageTemplate(
        action="head_knock",
        template="(head knock, {duration_ms}ms)",
    )
    manager = ESP32Manager(
        notify_config=_notify_config(
            legacy=False,
            channels=True,
            jsonl=False,
            messages=messages,
        )
    )

    await manager._emit_stackchan_event(_payload())

    assert notify_calls == [
        (
            stdio_server.CHANNEL_NOTIFICATION_METHOD,
            {
                "content": "(head knock, 350ms)",
                "meta": {
                    **_expected_meta(),
                    "action": "head_knock",
                },
            },
        )
    ]


@pytest.mark.asyncio
async def test_legacy_event_params_exclude_ts_unix(monkeypatch):
    notify_calls, log_calls = await _capture_emit(
        monkeypatch,
        _notify_config(legacy=True, channels=False, jsonl=False),
    )

    assert notify_calls == [
        (stdio_server.STACKCHAN_EVENT_METHOD, _expected_legacy_params())
    ]
    assert set(notify_calls[0][1]) == {
        "event_type",
        "subtype",
        "duration_ms",
        "action",
        "ts",
        "session_id",
    }
    assert log_calls == []


@pytest.mark.asyncio
async def test_channels_meta_includes_ts_unix(monkeypatch):
    notify_calls, log_calls = await _capture_emit(
        monkeypatch,
        _notify_config(legacy=False, channels=True, jsonl=False),
    )

    assert notify_calls == [
        (
            stdio_server.CHANNEL_NOTIFICATION_METHOD,
            {
                "content": DEFAULT_MESSAGE_TEMPLATES[("touch", "tap")].template,
                "meta": _expected_meta(),
            },
        )
    ]
    assert notify_calls[0][1]["meta"]["ts_unix"] == "1717000000.25"
    assert log_calls == []


@pytest.mark.asyncio
async def test_jsonl_payload_includes_ts_unix(monkeypatch):
    notify_calls, log_calls = await _capture_emit(
        monkeypatch,
        _notify_config(legacy=False, channels=False, jsonl=True),
    )

    assert notify_calls == []
    assert log_calls[0]["ts_unix"] == 1717000000.25


@pytest.mark.asyncio
async def test_mixed_legacy_and_channels(monkeypatch):
    notify_calls, log_calls = await _capture_emit(
        monkeypatch,
        _notify_config(legacy=True, channels=True, jsonl=False),
    )

    assert notify_calls == [
        (stdio_server.STACKCHAN_EVENT_METHOD, _expected_legacy_params()),
        (
            stdio_server.CHANNEL_NOTIFICATION_METHOD,
            {
                "content": DEFAULT_MESSAGE_TEMPLATES[("touch", "tap")].template,
                "meta": _expected_meta(),
            },
        ),
    ]
    legacy_params = notify_calls[0][1]
    channel_meta = notify_calls[1][1]["meta"]
    # Channel meta stringifies numeric fields per CC binary Zod schema; legacy
    # path keeps typed values. Compare common keys after stringification.
    expected_channel_subset = {
        key: str(value) if isinstance(value, (int, float)) else value
        for key, value in legacy_params.items()
    }
    assert {key: channel_meta[key] for key in legacy_params} == expected_channel_subset
    assert "ts_unix" in channel_meta
    assert "ts_unix" not in legacy_params
    assert log_calls == []


def test_http_session_uses_startup_notify_config(monkeypatch):
    startup_config = _notify_config(legacy=False, channels=True, jsonl=False)
    post_startup_config = _notify_config(legacy=False, channels=False, jsonl=False)
    load_calls = []

    def load_post_startup_config():
        load_calls.append("load")
        return post_startup_config

    monkeypatch.setattr(stdio_server, "load_notify_config", load_post_startup_config)
    app = build_app(
        CommandQueue(),
        gateway=_FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        notify_config=startup_config,
    )

    server = app.state.session_manager.app
    options = server.create_initialization_options()

    assert load_calls == []
    assert options.capabilities.experimental == {stdio_server.CHANNEL_CAPABILITY: {}}
    assert options.instructions == stdio_server.STACKCHAN_CHANNEL_INSTRUCTIONS


def _notify_config(
    *,
    legacy: bool,
    channels: bool,
    jsonl: bool,
    jsonl_path: Path = Path("/tmp/stackchan-events-test.jsonl"),
    messages: dict[tuple[str, str], MessageTemplate] | None = None,
) -> NotifyConfig:
    return NotifyConfig(
        legacy_event_enabled=legacy,
        channels_enabled=channels,
        jsonl_enabled=jsonl,
        jsonl_path=jsonl_path,
        messages=messages or dict(DEFAULT_MESSAGE_TEMPLATES),
    )


async def _capture_emit(monkeypatch, notify_config: NotifyConfig):
    notify_calls: list[tuple[str, dict]] = []
    log_calls: list[dict] = []

    async def fake_notify(method, params):
        notify_calls.append((method, params))

    def fake_log_event(**kwargs):
        log_calls.append(kwargs)

    monkeypatch.setattr(stdio_server, "notify_stackchan_event", fake_notify)
    monkeypatch.setattr(event_log, "log_event", fake_log_event)
    monkeypatch.setattr(esp32_client.time, "time", lambda: 1717000000.25)

    manager = ESP32Manager(notify_config=notify_config)
    await manager._emit_stackchan_event(_payload())

    return notify_calls, log_calls


def _payload() -> dict:
    return {
        "event_type": "touch",
        "subtype": "tap",
        "duration_ms": 350,
        "ts": 123456,
        "session_id": "session-1",
    }


def _expected_legacy_params() -> dict:
    return {
        "event_type": "touch",
        "subtype": "tap",
        "duration_ms": 350,
        "action": "head_pat",
        "ts": 123456,
        "session_id": "session-1",
    }


def _expected_meta() -> dict:
    return {
        "event_type": "touch",
        "subtype": "tap",
        "duration_ms": "350",
        "action": "head_pat",
        "ts": "123456",
        "ts_unix": "1717000000.25",
        "session_id": "session-1",
    }


class _FakeESP32:
    device_connected = True

    def get_status(self) -> dict:
        return {"connected": True}


class _FakeGateway:
    esp32 = _FakeESP32()
