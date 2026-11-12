from datetime import UTC, datetime
import pytest
from stackchan_mcp.agent_status import (
    HEARTBEAT_INTERVAL_SECONDS,
    SESSION_TTL_SECONDS,
    AgentSnapshot,
    AgentStatusStore,
    AgentTask,
    UpdateResult,
)

NOW = datetime(2026, 1, 1, tzinfo=UTC)


def make_snapshot(
    agent="codex", session="s1", sequence=0, status="working", stage="plan", tasks=()
):
    return AgentSnapshot(agent, session, sequence, NOW, status, stage, tuple(tasks))


def test_valid_input_and_text_sanitization():
    task = AgentTask("task-1", "hello" + chr(10) + "world" + chr(0), "pending")
    snap = make_snapshot(tasks=[task], stage="a" + chr(10) + "b" + chr(127))
    assert task.text == "helloworld" and snap.stage == "ab"


@pytest.mark.parametrize(
    "kwargs",
    [
        {"agent": "unknown"},
        {"agent": ""},
        {"session_id": ""},
        {"sequence": -1},
        {"status": "unknown"},
        {"stage": "x" * 25},
        {"observed_at": datetime(2026, 1, 1)},
    ],
)
def test_rejects_invalid_snapshots(kwargs):
    fields = dict(
        agent="codex",
        session_id="s1",
        sequence=0,
        observed_at=NOW,
        status="working",
        stage="",
        tasks=(),
    )
    fields.update(kwargs)
    with pytest.raises(ValueError):
        AgentSnapshot(**fields)


def test_rejects_task_limits():
    for args in [
        ("", "x", "pending"),
        ("t", "x" * 65, "pending"),
        ("t", "x", "unknown"),
    ]:
        with pytest.raises(ValueError):
            AgentTask(*args)
    with pytest.raises(ValueError):
        make_snapshot(tasks=[AgentTask(str(i), "x", "pending") for i in range(13)])
    with pytest.raises(ValueError):
        make_snapshot(
            tasks=[
                AgentTask("same", "x", "pending"),
                AgentTask("same", "y", "completed"),
            ]
        )


@pytest.mark.asyncio
async def test_full_replacement_and_agent_order():
    store = AgentStatusStore()
    first = make_snapshot(
        tasks=[AgentTask("a", "remove", "pending"), AgentTask("b", "remove", "pending")]
    )
    assert await store.update(first) == UpdateResult(True, first)
    new = make_snapshot(
        sequence=1, stage="", tasks=[AgentTask("a", "fresh", "completed")]
    )
    await store.update(new)
    await store.update(make_snapshot(agent="claude"))
    views = await store.get_all()
    assert (
        [v.snapshot.agent for v in views] == ["codex", "claude"]
        and views[0].snapshot.tasks == new.tasks
        and views[0].snapshot.stage == ""
    )


@pytest.mark.asyncio
async def test_sequence_rejection_and_session_fencing():
    store = AgentStatusStore()
    await store.update(make_snapshot(session="old", sequence=9))
    current = make_snapshot(session="new")
    await store.update(current)
    for candidate in (make_snapshot(session="old", sequence=10), current):
        result = await store.update(candidate)
        assert not result.accepted and result.current == current


@pytest.mark.asyncio
async def test_clear_only_current_session():
    store = AgentStatusStore()
    await store.update(make_snapshot(session="old"))
    await store.update(make_snapshot(session="new"))
    assert not await store.clear_session("codex", "old") and await store.clear_session(
        "codex", "new"
    )
    assert (await store.get_all())[0].snapshot is None


@pytest.mark.asyncio
async def test_explicit_monotonic_now_marks_expired_view_stale():
    store = AgentStatusStore()
    snap = make_snapshot(tasks=[AgentTask("t", "remain", "pending")])
    await store.update(snap)
    fresh = (await store.get_all())[0]
    assert not fresh.stale and fresh.received_at is not None
    view = (await store.get_all(fresh.received_at + SESSION_TTL_SECONDS))[0]
    assert (
        view.stale
        and not view.online
        and view.snapshot.tasks == snap.tasks
        and view.snapshot.stage == snap.stage
    )


def test_timing_constants():
    assert SESSION_TTL_SECONDS == 45 and HEARTBEAT_INTERVAL_SECONDS == 15
