from __future__ import annotations

import asyncio
import json

import pytest

from stackchan_mcp.agent_adapters import runner
from stackchan_mcp.gateway import Gateway
from stackchan_mcp.stdio_server import _dispatch_mcp_tool


def test_plan_is_private_sanitized_and_bound_to_one_session(tmp_path, monkeypatch):
    from stackchan_mcp.agent_adapters.codex_plan import read_plan, write_plan

    monkeypatch.setenv("STACKCHAN_AGENT_PLANS_DIR", str(tmp_path))
    write_plan("desktop-1", [{"step": "验证\n设备", "status": "in_progress", "private": "不要上传"}], "设备验证")
    plan = read_plan("desktop-1")
    assert plan["tasks"] == [{"id": "desktop-1:1", "text": "验证 设备", "status": "in_progress"}]
    assert "不要上传" not in json.dumps(plan, ensure_ascii=False)
    assert read_plan("desktop-2") is None
    assert (tmp_path / "desktop-1.json").stat().st_mode & 0o777 == 0o600
    write_plan("desktop-1", [], "实现")
    assert read_plan("desktop-1")["tasks"] == []


@pytest.mark.parametrize("session,plan", [
    ("../private", []),
    ("desktop-1", [{"step": "无效", "status": "unknown"}]),
    ("desktop-1", [{"step": "任务", "status": "pending"}] * 13),
])
def test_invalid_plan_does_not_replace_last_valid_plan(tmp_path, monkeypatch, session, plan):
    from stackchan_mcp.agent_adapters.codex_plan import read_plan, write_plan

    monkeypatch.setenv("STACKCHAN_AGENT_PLANS_DIR", str(tmp_path))
    write_plan("desktop-1", [{"step": "保留", "status": "pending"}], "计划")
    with pytest.raises(ValueError):
        write_plan(session, plan, "计划")
    assert read_plan("desktop-1")["tasks"][0]["text"] == "保留"


@pytest.mark.asyncio
async def test_watcher_publishes_plan_edits_and_keeps_tasks_through_turn_completion(tmp_path, monkeypatch):
    from stackchan_mcp.agent_adapters.codex_plan import write_plan

    monkeypatch.setenv("STACKCHAN_AGENT_PLANS_DIR", str(tmp_path / "plans"))
    monkeypatch.setenv("CODEX_SESSIONS_DIR", str(tmp_path / "sessions"))
    rollout = tmp_path / "sessions/main.jsonl"
    rollout.parent.mkdir()
    rollout.write_text(json.dumps({"type": "session_meta", "payload": {
        "id": "desktop-1", "originator": "Codex Desktop", "source": "vscode",
    }}) + "\n" + json.dumps({"type": "event_msg", "payload": {"type": "task_started"}}) + "\n")
    write_plan("desktop-1", [{"step": "设备验证", "status": "pending"}], "验证")
    gateway = Gateway()
    snapshots = []

    async def publish(snapshot):
        snapshots.append(snapshot)
        result = await _dispatch_mcp_tool("agent_update_snapshot", runner._heartbeat(snapshot), gateway)
        assert json.loads(result[0].text)["accepted"] is True
        if len(snapshots) == 1:
            write_plan("desktop-1", [{"step": "设备验证", "status": "completed"}], "验证")
        elif len(snapshots) == 2:
            with rollout.open("a") as stream:
                stream.write(json.dumps({"type": "event_msg", "payload": {"type": "task_complete"}}) + "\n")
        else:
            raise asyncio.CancelledError

    monkeypatch.setattr(runner, "_publish", publish)
    with pytest.raises(asyncio.CancelledError):
        await asyncio.wait_for(runner._watch_codex(), timeout=4)
    assert [s["tasks"][0]["status"] for s in snapshots] == ["pending", "completed", "completed"]
    assert [s["status"] for s in snapshots] == ["working", "working", "completed"]
    assert snapshots[0]["stage"] == "验证"
    assert snapshots[2]["stage"] == "已完成"
