from __future__ import annotations

import asyncio
import json
import os

import pytest

from stackchan_mcp.agent_adapters import runner
from stackchan_mcp.gateway import Gateway
from stackchan_mcp.stdio_server import _dispatch_mcp_tool


def append_record(path, session, kind, content, stamp, **extra):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as stream:
        stream.write(json.dumps({
            "sessionId": session, "type": kind,
            "message": {"content": content}, **extra,
        }) + "\n")
    os.utime(path, ns=(stamp, stamp))


@pytest.mark.asyncio
async def test_claude_watcher_preserves_tasks_and_switches_back_to_previous_thread(tmp_path, monkeypatch):
    monkeypatch.setenv("CLAUDE_PROJECTS_DIR", str(tmp_path))
    path_a = tmp_path / "project" / "a.jsonl"
    path_b = tmp_path / "project" / "b.jsonl"
    append_record(path_a, "session-a", "assistant", [{
        "type": "tool_use", "id": "create-1", "name": "TaskCreate", "input": {},
    }], 1_000_000_000)
    append_record(path_a, "session-a", "user", [{
        "type": "tool_result", "tool_use_id": "create-1",
    }], 1_000_000_000, toolUseResult={"task": {"id": "1", "subject": "编译固件"}})
    # A deeper child transcript, even newer, cannot take over the right column.
    append_record(tmp_path / "project" / "subagents" / "child.jsonl",
                  "child", "user", "private text", 9_000_000_000)
    gateway = Gateway()
    snapshots, accepted = [], []

    async def publish(snapshot):
        snapshots.append(snapshot)
        result = await _dispatch_mcp_tool("agent_update_snapshot", runner._heartbeat(snapshot), gateway)
        accepted.append(json.loads(result[0].text)["accepted"])
        if len(snapshots) == 1:
            append_record(path_b, "session-b", "user", "private text", 2_000_000_000)
        elif len(snapshots) == 2:
            append_record(path_a, "session-a", "assistant", [], 3_000_000_000,
                          message={"content": [], "stop_reason": "end_turn"})
        else:
            raise asyncio.CancelledError

    monkeypatch.setattr(runner, "_publish", publish)
    with pytest.raises(asyncio.CancelledError):
        await asyncio.wait_for(runner._watch_claude(), timeout=4)
    assert accepted == [True, True, True]
    assert [snapshot["status"] for snapshot in snapshots] == ["working", "working", "completed"]
    assert snapshots[0]["tasks"] == snapshots[2]["tasks"] == [
        {"id": "task-1", "text": "编译固件", "status": "pending"},
    ]
    assert snapshots[0]["session_id"] != snapshots[2]["session_id"]


@pytest.mark.asyncio
async def test_claude_watcher_retries_transient_publish_failure(tmp_path, monkeypatch):
    monkeypatch.setenv("CLAUDE_PROJECTS_DIR", str(tmp_path))
    append_record(tmp_path / "project" / "a.jsonl", "session-a", "user", "private", 1_000_000_000)
    attempts = []

    async def publish(snapshot):
        attempts.append(snapshot)
        if len(attempts) == 1:
            raise ConnectionError
        raise asyncio.CancelledError

    monkeypatch.setattr(runner, "_publish", publish)
    with pytest.raises(asyncio.CancelledError):
        await asyncio.wait_for(runner._watch_claude(), timeout=4)
    assert len(attempts) == 2
    assert attempts[0]["tasks"] == attempts[1]["tasks"]
