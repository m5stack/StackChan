from __future__ import annotations

import asyncio
import json

import pytest

from stackchan_mcp.agent_adapters import (
    normalize_claude_hook,
    normalize_codex_event,
    codex_desktop_session_id,
)


def test_claude_todowrite_hook_keeps_only_short_task_fields():
    snapshot = normalize_claude_hook(
        {
            "session_id": "claude-session-1",
            "hook_event_name": "PostToolUse",
            "tool_name": "TodoWrite",
            "tool_input": {
                "todos": [
                    {"content": "编译网关固件", "status": "in_progress", "activeForm": "正在编译"},
                    {"content": "完成验证", "status": "pending", "activeForm": "正在验证"},
                ],
                "private_prompt": "must not escape",
            },
            "cwd": "/private/project",
        },
        sequence=4,
    )

    assert snapshot is not None
    assert snapshot.pop("observed_at")
    assert snapshot == {
        "agent": "claude",
        "session_id": "claude-session-1",
        "sequence": 4,
        "status": "working",
        "stage": "计划",
        "tasks": [
            {"id": "claude-session-1:1", "text": "编译网关固件", "status": "in_progress"},
            {"id": "claude-session-1:2", "text": "完成验证", "status": "pending"},
        ],
    }


def test_claude_hook_ignores_unrelated_tool_payloads():
    assert normalize_claude_hook(
        {"session_id": "claude-session-1", "hook_event_name": "PreToolUse", "tool_name": "Read", "tool_input": {"file_path": "/secret"}},
        sequence=1,
    ) is None


def test_codex_desktop_plan_event_maps_status_without_message_content():
    snapshot = normalize_codex_event(
        {
            "type": "turn_plan_updated",
            "thread_id": "codex-thread-1",
            "plan": [
                {"step": "新增适配器", "status": "completed"},
                {"step": "验证网关", "status": "inProgress"},
            ],
            "message": "private conversation text",
        },
        sequence=8,
    )

    assert snapshot is not None
    assert snapshot.pop("observed_at")
    assert snapshot == {
        "agent": "codex",
        "session_id": "codex-thread-1",
        "sequence": 8,
        "status": "working",
        "stage": "计划",
        "tasks": [
            {"id": "codex-thread-1:1", "text": "新增适配器", "status": "completed"},
            {"id": "codex-thread-1:2", "text": "验证网关", "status": "in_progress"},
        ],
    }


def test_codex_event_ignores_chat_messages():
    assert normalize_codex_event(
        {"type": "response_item", "message": "private conversation text"}, sequence=1
    ) is None


def test_codex_desktop_marker_excludes_cli_rollouts():
    assert codex_desktop_session_id(
        {"type": "session_meta", "payload": {"id": "desktop-1", "originator": "Codex Desktop"}}
    ) == "desktop-1"
    assert codex_desktop_session_id(
        {"type": "session_meta", "payload": {"id": "cli-1", "originator": "codex_cli_rs"}}
    ) is None


def test_codex_desktop_marker_excludes_child_agent_rollouts():
    assert codex_desktop_session_id({
        "type": "session_meta", "payload": {
            "id": "child-1", "originator": "Codex Desktop", "source": {"subagent": {}}
        }
    }) is None


def test_reader_waits_for_complete_record(tmp_path):
    from stackchan_mcp.agent_adapters.runner import _read_records
    path = tmp_path / "events.jsonl"
    path.write_bytes(b'{"type":"first"}\n{"type":"second"')
    records, offset = _read_records(path, 0)
    assert records == [{"type": "first"}]
    with path.open("ab") as stream:
        stream.write(b'}\n')
    records, _ = _read_records(path, offset)
    assert records == [{"type": "second"}]


@pytest.mark.asyncio
async def test_heartbeat_renews_gateway_lease_without_new_agent_event():
    from stackchan_mcp.agent_adapters.runner import _heartbeat
    from stackchan_mcp.gateway import Gateway
    from stackchan_mcp.stdio_server import _dispatch_mcp_tool
    gateway = Gateway()
    snapshot = normalize_codex_event({
        "type": "task_started", "thread_id": "desktop-1"
    }, sequence=1)
    for _ in range(2):
        result = await _dispatch_mcp_tool("agent_update_snapshot", _heartbeat(snapshot), gateway)
        assert json.loads(result[0].text)["accepted"] is True


@pytest.mark.asyncio
async def test_watcher_restores_latest_parent_rollout_not_older_file(tmp_path, monkeypatch):
    import os
    import stackchan_mcp.agent_adapters.runner as runner

    def rollout(name, event_type, modified):
        path = tmp_path / name
        path.write_text(json.dumps({"type": "session_meta", "payload": {
            "id": "desktop-1", "originator": "Codex Desktop", "source": "vscode"
        }}) + '\n' + json.dumps({"type": "event_msg", "payload": {
            "type": event_type
        }}) + '\n')
        os.utime(path, ns=(modified, modified))

    rollout("new.jsonl", "task_started", 2_000_000_000)
    rollout("old.jsonl", "task_complete", 1_000_000_000)
    monkeypatch.setenv("CODEX_SESSIONS_DIR", str(tmp_path))
    published = []

    async def publish(snapshot):
        published.append(snapshot.copy())
        raise asyncio.CancelledError

    monkeypatch.setattr(runner, "_publish", publish)
    with pytest.raises(asyncio.CancelledError):
        await runner._watch_codex()
    assert published[0]["status"] == "working"


@pytest.mark.asyncio
async def test_watcher_can_return_to_a_previously_active_desktop_thread(tmp_path, monkeypatch):
    import os
    import stackchan_mcp.agent_adapters.runner as runner
    from stackchan_mcp.gateway import Gateway
    from stackchan_mcp.stdio_server import _dispatch_mcp_tool

    def write_session(name, event, stamp):
        path = tmp_path / f"{name}.jsonl"
        metadata = '' if path.exists() else json.dumps({"type": "session_meta", "payload": {
            "id": name, "originator": "Codex Desktop", "source": "vscode"
        }}) + '\n'
        with path.open('a') as stream:
            stream.write(metadata + json.dumps({"type": "event_msg", "payload": {"type": event}}) + '\n')
        os.utime(path, ns=(stamp, stamp))

    write_session("thread-a", "task_started", 1_000_000_000)
    monkeypatch.setenv("CODEX_SESSIONS_DIR", str(tmp_path))
    gateway = Gateway()
    accepted = []

    async def publish(snapshot):
        result = await _dispatch_mcp_tool("agent_update_snapshot", runner._heartbeat(snapshot), gateway)
        accepted.append(json.loads(result[0].text)["accepted"])
        if len(accepted) == 1:
            write_session("thread-b", "task_started", 2_000_000_000)
        elif len(accepted) == 2:
            write_session("thread-a", "task_complete", 3_000_000_000)
        else:
            raise asyncio.CancelledError

    monkeypatch.setattr(runner, "_publish", publish)
    with pytest.raises(asyncio.CancelledError):
        await asyncio.wait_for(runner._watch_codex(), timeout=4)
    assert accepted == [True, True, True]
