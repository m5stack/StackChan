"""Process-local, privacy-limited status snapshots for coding agents."""

import asyncio
import re
import time
from dataclasses import dataclass
from datetime import datetime
from typing import Literal

AgentStatus = Literal["idle", "working", "waiting", "completed", "failed"]
TaskStatus = Literal["pending", "in_progress", "completed"]
HEARTBEAT_INTERVAL_SECONDS = 15
SESSION_TTL_SECONDS = 45
MAX_ID_LENGTH = 128
MAX_SESSION_ID_LENGTH = 128
MAX_STAGE_LENGTH = 24
MAX_TASK_TEXT_LENGTH = 64
MAX_TASKS = 12

_AGENT_STATUSES = {"idle", "working", "waiting", "completed", "failed"}
_TASK_STATUSES = {"pending", "in_progress", "completed"}
_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]*$")


def _valid_id(value: object, maximum: int) -> bool:
    return (
        isinstance(value, str)
        and 0 < len(value) <= maximum
        and _ID_RE.fullmatch(value) is not None
    )


def _clean_text(value: object, maximum: int, field: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{field} must be text")
    cleaned = "".join(
        char for char in value if not (ord(char) < 32 or 127 <= ord(char) < 160)
    )
    if len(cleaned) > maximum:
        raise ValueError(f"{field} exceeds {maximum} Unicode code points")
    return cleaned


@dataclass(frozen=True, slots=True)
class AgentTask:
    id: str
    text: str
    status: TaskStatus

    def __post_init__(self) -> None:
        if not _valid_id(self.id, MAX_ID_LENGTH):
            raise ValueError("task id is invalid")
        object.__setattr__(
            self, "text", _clean_text(self.text, MAX_TASK_TEXT_LENGTH, "task text")
        )
        if self.status not in _TASK_STATUSES:
            raise ValueError("task status is invalid")


@dataclass(frozen=True, slots=True)
class AgentSnapshot:
    agent: str
    session_id: str
    sequence: int
    observed_at: datetime
    status: AgentStatus
    stage: str
    tasks: tuple[AgentTask, ...]

    def __post_init__(self) -> None:
        if self.agent not in ("codex", "claude"):
            raise ValueError("agent must be codex or claude")
        if not _valid_id(self.session_id, MAX_SESSION_ID_LENGTH):
            raise ValueError("session id is invalid")
        if (
            isinstance(self.sequence, bool)
            or not isinstance(self.sequence, int)
            or self.sequence < 0
        ):
            raise ValueError("sequence must be a non-negative integer")
        if (
            not isinstance(self.observed_at, datetime)
            or self.observed_at.utcoffset() is None
        ):
            raise ValueError("observed_at must be timezone-aware")
        if self.status not in _AGENT_STATUSES:
            raise ValueError("agent status is invalid")
        object.__setattr__(
            self, "stage", _clean_text(self.stage, MAX_STAGE_LENGTH, "stage")
        )
        tasks = tuple(self.tasks)
        if len(tasks) > MAX_TASKS or any(
            not isinstance(task, AgentTask) for task in tasks
        ):
            raise ValueError("tasks must contain at most 12 AgentTask values")
        if len({task.id for task in tasks}) != len(tasks):
            raise ValueError("task ids must be unique")
        object.__setattr__(self, "tasks", tasks)


@dataclass(frozen=True, slots=True)
class AgentSnapshotView:
    snapshot: AgentSnapshot | None
    online: bool
    stale: bool
    received_at: float | None


@dataclass(frozen=True, slots=True)
class UpdateResult:
    accepted: bool
    current: AgentSnapshot | None


class AgentStatusStore:
    """Async-safe latest snapshot store with a lease per agent."""

    def __init__(self) -> None:
        self._lock = asyncio.Lock()
        self._current: dict[str, tuple[AgentSnapshot, float]] = {}
        self._retired_sessions: dict[str, set[str]] = {"codex": set(), "claude": set()}

    async def update(self, snapshot: AgentSnapshot) -> UpdateResult:
        if not isinstance(snapshot, AgentSnapshot):
            raise ValueError("snapshot must be an AgentSnapshot")
        async with self._lock:
            previous = self._current.get(snapshot.agent)
            if previous is None:
                if snapshot.session_id in self._retired_sessions[snapshot.agent]:
                    return UpdateResult(False, None)
            elif snapshot.session_id == previous[0].session_id:
                if snapshot.sequence <= previous[0].sequence:
                    return UpdateResult(False, previous[0])
            else:
                if snapshot.session_id in self._retired_sessions[snapshot.agent]:
                    return UpdateResult(False, previous[0])
                self._retired_sessions[snapshot.agent].add(previous[0].session_id)
            self._current[snapshot.agent] = (snapshot, time.monotonic())
            return UpdateResult(True, snapshot)

    async def get_all(self, now: float | None = None) -> tuple[AgentSnapshotView, ...]:
        current_time = time.monotonic() if now is None else now
        async with self._lock:
            views = []
            for agent in ("codex", "claude"):
                item = self._current.get(agent)
                if item is None:
                    views.append(AgentSnapshotView(None, False, False, None))
                    continue
                snapshot, received_at = item
                stale = current_time - received_at >= SESSION_TTL_SECONDS
                views.append(AgentSnapshotView(snapshot, not stale, stale, received_at))
            return tuple(views)

    async def clear_session(self, agent: str, session_id: str) -> bool:
        async with self._lock:
            current = self._current.get(agent)
            if current is None or current[0].session_id != session_id:
                return False
            del self._current[agent]
            self._retired_sessions[agent].add(session_id)
            return True
