"""Explicit, session-bound plans for Desktop builds without plan events."""

from __future__ import annotations

from dataclasses import asdict
import json
import os
from pathlib import Path
import re
import tempfile
import time
from typing import Any

from . import _tasks
from ..agent_status import AgentTask

_SESSION_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}\Z")


def _path(session_id: str) -> Path:
    if not isinstance(session_id, str) or not _SESSION_ID.fullmatch(session_id):
        raise ValueError("请提供有效的 Codex 主会话 ID")
    root = Path(os.environ.get(
        "STACKCHAN_AGENT_PLANS_DIR",
        Path.home() / ".local/share/stackchan-agent-dashboard/plans",
    ))
    return root / f"{session_id}.json"


def write_plan(session_id: str, plan: Any, stage: str = "计划") -> None:
    path = _path(session_id)
    if not isinstance(plan, list) or len(plan) > 12:
        raise ValueError("plan 必须是最多 12 项的列表")
    tasks = _tasks(plan, session_id[:95])
    if len(tasks) != len(plan):
        raise ValueError("每项计划须包含非空 step 和有效的 status")
    tasks = [asdict(AgentTask(**task)) for task in tasks]
    if not isinstance(stage, str):
        raise ValueError("stage 必须是文字")
    stage = " ".join(stage.split())[:24]
    payload = {"session_id": session_id, "revision": time.time_ns(), "stage": stage, "tasks": tasks}
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(payload, stream, ensure_ascii=False)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def read_plan(session_id: str) -> dict[str, Any] | None:
    try:
        payload = json.loads(_path(session_id).read_text())
        if not isinstance(payload, dict) or payload.get("session_id") != session_id:
            return None
        revision, stage, tasks = payload["revision"], payload["stage"], payload["tasks"]
        if (type(revision) is not int or revision < 0 or not isinstance(stage, str)
                or len(stage) > 24 or not isinstance(tasks, list) or len(tasks) > 12):
            return None
        tasks = [asdict(AgentTask(**task)) for task in tasks]
        return {"revision": revision, "stage": stage, "tasks": tasks}
    except (OSError, ValueError, TypeError, KeyError):
        return None
