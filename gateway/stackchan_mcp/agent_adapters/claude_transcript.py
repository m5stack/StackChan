"""Read Claude Code lifecycle and task records without retaining chat text."""

from __future__ import annotations

import re
import time
from typing import Any

from . import _snapshot, _tasks

_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}\Z")


class ClaudeTranscript:
    """A stateful parser for one primary Claude Code transcript."""

    def __init__(self) -> None:
        self.session_id = ""
        self.tasks: dict[str, dict[str, str]] = {}
        self.pending: dict[str, tuple[str, dict[str, Any]]] = {}

    def consume(self, record: dict[str, Any]) -> dict[str, Any] | None:
        session_id = record.get("sessionId")
        if (
            record.get("isSidechain")
            or not isinstance(session_id, str)
            or not _ID.fullmatch(session_id)
        ):
            return None
        if self.session_id != session_id:
            self.session_id = session_id
            self.tasks.clear()
            self.pending.clear()
        kind = record.get("type")
        message = record.get("message")
        if kind not in {"assistant", "user"} or not isinstance(message, dict):
            return None
        content = message.get("content")
        blocks = content if isinstance(content, list) else []
        status, stage = "working", "处理中"
        for block in blocks:
            if not isinstance(block, dict):
                continue
            if block.get("type") == "tool_use":
                name, call_id = block.get("name"), block.get("id")
                if name == "AskUserQuestion":
                    status, stage = "waiting", "等待输入"
                if not isinstance(call_id, str):
                    continue
                fields = block.get("input")
                if not isinstance(fields, dict):
                    continue
                if name == "TodoWrite":
                    fields = {"todos": _tasks(fields.get("todos"), "todo")}
                elif name == "TaskUpdate":
                    fields = {key: fields[key] for key in ("taskId", "status", "subject") if key in fields}
                elif name == "TaskCreate":
                    fields = {}  # Read the title and ID only from a successful result.
                else:
                    continue
                self.pending[call_id] = (name, fields)
            elif block.get("type") == "tool_result":
                call_id = block.get("tool_use_id")
                update = self.pending.pop(call_id, None) if isinstance(call_id, str) else None
                if update and not block.get("is_error"):
                    self._apply(*update, record.get("toolUseResult"))
                    stage = "任务更新"
        reason = message.get("stop_reason")
        if kind == "assistant" and reason == "end_turn":
            status, stage = "completed", "已完成"
        elif reason == "error":
            status, stage = "failed", "执行失败"
        priority = {"in_progress": 0, "pending": 1, "completed": 2}
        visible_tasks = sorted(self.tasks.values(), key=lambda task: priority[task["status"]])[:12]
        return _snapshot("claude", session_id, time.time_ns(), status, stage, visible_tasks)

    def _apply(self, name: str, fields: dict[str, Any], result: Any) -> None:
        if name == "TodoWrite":
            self.tasks = {task["id"]: task for task in fields["todos"]}
            return
        if not isinstance(result, dict):
            return
        if name == "TaskCreate":
            task = result.get("task")
            if not isinstance(task, dict):
                return
            task_id, title = task.get("id"), task.get("subject")
            if isinstance(task_id, str) and _ID.fullmatch(task_id) and isinstance(title, str):
                self.tasks[task_id] = {"id": f"task-{task_id}"[:128], "text": " ".join(title.split())[:64], "status": "pending"}
        elif name == "TaskUpdate" and result.get("success") is True:
            task_id = fields.get("taskId")
            if not isinstance(task_id, str) or task_id not in self.tasks:
                return
            status = fields.get("status")
            if status == "deleted":
                del self.tasks[task_id]
                return
            if status in {"pending", "in_progress", "completed"}:
                self.tasks[task_id]["status"] = status
            title = fields.get("subject")
            if isinstance(title, str):
                self.tasks[task_id]["text"] = " ".join(title.split())[:64]
