"""Privacy-limited adapters for local coding-agent events."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any


def _tasks(raw_tasks: object, session_id: str) -> list[dict[str, str]]:
    if not isinstance(raw_tasks, list):
        return []
    result = []
    for item in raw_tasks[:12]:
        if not isinstance(item, dict):
            continue
        text = item.get("content", item.get("step"))
        status = item.get("status")
        if not isinstance(text, str) or not isinstance(status, str):
            continue
        mapped = {
            "pending": "pending",
            "inProgress": "in_progress",
            "in_progress": "in_progress",
            "completed": "completed",
        }.get(status)
        text = " ".join(text.split())[:64]
        if text and mapped:
            result.append(
                {"id": f"{session_id}:{len(result) + 1}", "text": text, "status": mapped}
            )
    return result


def _snapshot(
    agent: str,
    session_id: object,
    sequence: int,
    status: str,
    stage: str,
    tasks: list[dict[str, str]],
) -> dict[str, Any] | None:
    if not isinstance(session_id, str) or not session_id or sequence < 0:
        return None
    return {
        "agent": agent,
        "session_id": session_id[:128],
        "sequence": sequence,
        "observed_at": datetime.now(timezone.utc).isoformat(),
        "status": status,
        "stage": stage,
        "tasks": tasks,
    }


def normalize_claude_hook(event: dict[str, Any], *, sequence: int) -> dict[str, Any] | None:
    """Map a Claude Code hook event without retaining unrelated tool input."""
    event_name = event.get("hook_event_name")
    session_id = event.get("session_id")
    tool_name = event.get("tool_name")
    status, stage, tasks = "working", "处理中", []
    if event_name == "PostToolUse" and tool_name == "TodoWrite":
        tool_input = event.get("tool_input")
        raw_tasks = tool_input.get("todos") if isinstance(tool_input, dict) else None
        tasks = _tasks(raw_tasks, str(session_id or "claude"))
        if tasks and all(task["status"] == "completed" for task in tasks):
            status, stage = "completed", "已完成"
        else:
            stage = "计划"
    elif event_name == "SessionStart":
        stage = "已启动"
    elif event_name == "Stop":
        status, stage = "completed", "已完成"
    elif event_name == "Notification":
        status, stage = "waiting", "等待输入"
    elif event_name not in {"UserPromptSubmit", "SubagentStart", "SubagentStop"}:
        return None
    return _snapshot("claude", session_id, sequence, status, stage, tasks)


def normalize_codex_event(event: dict[str, Any], *, sequence: int) -> dict[str, Any] | None:
    """Map a Codex desktop plan or turn event, excluding message content."""
    payload = event.get("payload") if event.get("type") == "event_msg" else event
    if not isinstance(payload, dict):
        return None
    event_type = payload.get("type")
    thread_id = payload.get("thread_id", event.get("session_id", event.get("thread_id")))
    status, stage, tasks = "working", "处理中", []
    if event_type in {"turn_plan_updated", "plan_updated"}:
        tasks = _tasks(payload.get("plan"), str(thread_id or "codex"))
        stage = "计划" if tasks else "实现"
        if tasks and all(task["status"] == "completed" for task in tasks):
            status, stage = "completed", "已完成"
    elif event_type in {"task_started", "turn_started"}:
        stage = "开始工作"
    elif event_type in {"task_complete", "turn_completed"}:
        status, stage = "completed", "已完成"
    elif event_type in {"turn_aborted", "turn_interrupted"}:
        status, stage = "waiting", "已中断"
    else:
        return None
    return _snapshot("codex", thread_id, sequence, status, stage, tasks)


def normalize_codex_record(
    record: dict[str, Any], *, sequence: int, session_id: str | None = None
) -> dict[str, Any] | None:
    """Normalize a desktop JSONL record after checking its source marker."""
    if record.get("type") == "session_meta":
        payload = record.get("payload")
        if isinstance(payload, dict) and payload.get("originator") == "Codex Desktop":
            return {"session_id": payload.get("id"), "_session_meta": True}
        return None
    if record.get("type") != "event_msg":
        return None
    payload = record.get("payload")
    if not isinstance(payload, dict):
        return None
    return normalize_codex_event(
        {
            "type": "event_msg",
            "session_id": session_id or record.get("session_id"),
            "payload": payload,
        },
        sequence=sequence,
    )


def codex_desktop_session_id(record: dict[str, Any]) -> str | None:
    """Return a session ID only for records explicitly marked Codex Desktop."""
    payload = record.get("payload") if isinstance(record, dict) else None
    if not isinstance(payload, dict) or record.get("type") != "session_meta":
        return None
    if isinstance(payload.get("source"), dict) and "subagent" in payload["source"]:
        return None
    session_id = payload.get("id")
    return session_id if payload.get("originator") == "Codex Desktop" and isinstance(session_id, str) else None
