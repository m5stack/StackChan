"""Tests for #177 Phase A ownership lock."""

from __future__ import annotations

import json
import os
from datetime import datetime
from pathlib import Path

import pytest

from stackchan_mcp import ownership as ownership_module
from stackchan_mcp.ownership import (
    OwnershipError,
    acquire_lock,
    generate_owner_id,
    get_process_start_time,
    is_pid_alive,
    read_lock,
    release_lock,
    release_lock_if_owner,
)


@pytest.fixture
def lock_path(tmp_path: Path) -> Path:
    return tmp_path / "owner.lock"


def test_acquire_when_no_lock_succeeds(lock_path: Path) -> None:
    info = acquire_lock("test-owner-1", lock_path)
    assert info["owner_id"] == "test-owner-1"
    assert info["pid"] == os.getpid()
    assert lock_path.exists()
    data = json.loads(lock_path.read_text(encoding="utf-8"))
    assert data["owner_id"] == "test-owner-1"


def test_acquire_writes_proc_start_epoch_when_available(
    monkeypatch: pytest.MonkeyPatch, lock_path: Path
) -> None:
    monkeypatch.setattr(
        ownership_module, "get_process_start_time", lambda pid: 1234.5
    )

    info = acquire_lock("test-owner-1", lock_path)

    assert info["proc_start_epoch"] == 1234.5
    data = json.loads(lock_path.read_text(encoding="utf-8"))
    assert data["proc_start_epoch"] == 1234.5


def test_acquire_when_live_lock_refuses(lock_path: Path) -> None:
    acquire_lock("first-owner", lock_path)
    with pytest.raises(OwnershipError, match="already owned by first-owner"):
        acquire_lock("second-owner", lock_path)


def test_acquire_when_stale_lock_overwrites(lock_path: Path) -> None:
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "dead-owner",
                "pid": 999999,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "nowhere",
            }
        ),
        encoding="utf-8",
    )
    info = acquire_lock("new-owner", lock_path)
    assert info["owner_id"] == "new-owner"
    data = json.loads(lock_path.read_text(encoding="utf-8"))
    assert data["owner_id"] == "new-owner"


def test_acquire_when_live_pid_has_wrong_proc_start_overwrites(
    monkeypatch: pytest.MonkeyPatch, lock_path: Path
) -> None:
    live_pid = 4242
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "recycled-owner",
                "pid": live_pid,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": 1000.0,
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(ownership_module, "is_pid_alive", lambda pid: True)

    def fake_start_time(pid: int) -> float | None:
        if pid == live_pid:
            return 2000.0
        if pid == os.getpid():
            return 3000.0
        return None

    monkeypatch.setattr(
        ownership_module, "get_process_start_time", fake_start_time
    )

    info = acquire_lock("new-owner", lock_path)

    assert info["owner_id"] == "new-owner"
    assert info["proc_start_epoch"] == 3000.0
    data = json.loads(lock_path.read_text(encoding="utf-8"))
    assert data["owner_id"] == "new-owner"


def test_acquire_when_live_pid_has_matching_proc_start_refuses(
    monkeypatch: pytest.MonkeyPatch, lock_path: Path
) -> None:
    live_pid = 4242
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "live-owner",
                "pid": live_pid,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": 1000.0,
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(ownership_module, "is_pid_alive", lambda pid: True)
    monkeypatch.setattr(
        ownership_module,
        "get_process_start_time",
        lambda pid: 1001.5 if pid == live_pid else None,
    )

    with pytest.raises(OwnershipError, match="already owned by live-owner"):
        acquire_lock("new-owner", lock_path)


def test_acquire_when_old_schema_live_lock_refuses_without_start_check(
    monkeypatch: pytest.MonkeyPatch, lock_path: Path
) -> None:
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "old-owner",
                "pid": 4242,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(ownership_module, "is_pid_alive", lambda pid: True)

    def fail_start_time(pid: int) -> float | None:
        pytest.fail("old-schema locks must fall back to PID-only checks")

    monkeypatch.setattr(
        ownership_module, "get_process_start_time", fail_start_time
    )

    with pytest.raises(OwnershipError, match="already owned by old-owner"):
        acquire_lock("new-owner", lock_path)


def test_acquire_when_proc_start_cannot_be_verified_refuses(
    monkeypatch: pytest.MonkeyPatch, lock_path: Path
) -> None:
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "unverified-owner",
                "pid": 4242,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": 1000.0,
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(ownership_module, "is_pid_alive", lambda pid: True)
    monkeypatch.setattr(
        ownership_module, "get_process_start_time", lambda pid: None
    )

    with pytest.raises(OwnershipError, match="already owned by unverified-owner"):
        acquire_lock("new-owner", lock_path)


def test_release_is_idempotent(lock_path: Path) -> None:
    acquire_lock("owner", lock_path)
    release_lock(lock_path)
    assert not lock_path.exists()
    release_lock(lock_path)


def test_release_if_owner_compares_proc_start_when_both_have_it(
    lock_path: Path,
) -> None:
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "owner",
                "pid": 123,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": 1000.0,
            }
        ),
        encoding="utf-8",
    )
    assert (
        release_lock_if_owner(
            {
                "owner_id": "owner",
                "pid": 123,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": 2000.0,
            },
            lock_path,
        )
        is False
    )
    assert lock_path.exists()


def test_release_if_owner_falls_back_when_proc_start_missing(
    lock_path: Path,
) -> None:
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "owner",
                "pid": 123,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
            }
        ),
        encoding="utf-8",
    )
    assert (
        release_lock_if_owner(
            {
                "owner_id": "owner",
                "pid": 123,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": 2000.0,
            },
            lock_path,
        )
        is True
    )
    assert not lock_path.exists()


def test_read_lock_returns_none_when_missing(lock_path: Path) -> None:
    assert read_lock(lock_path) is None


def test_read_lock_parses_proc_start_epoch(lock_path: Path) -> None:
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "owner",
                "pid": 123,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": 1000,
            }
        ),
        encoding="utf-8",
    )

    info = read_lock(lock_path)

    assert info is not None
    assert info["proc_start_epoch"] == 1000.0


def test_read_lock_ignores_invalid_proc_start_epoch(lock_path: Path) -> None:
    lock_path.write_text(
        json.dumps(
            {
                "owner_id": "owner",
                "pid": 123,
                "start_ts": "2000-01-01T00:00:00Z",
                "host": "test-host",
                "proc_start_epoch": "invalid",
            }
        ),
        encoding="utf-8",
    )

    info = read_lock(lock_path)

    assert info is not None
    assert "proc_start_epoch" not in info


def test_is_pid_alive_for_self_returns_true() -> None:
    assert is_pid_alive(os.getpid()) is True


def test_is_pid_alive_for_dead_pid_returns_false() -> None:
    assert is_pid_alive(999999) is False


def test_get_process_start_time_linux_reads_proc_stat(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    proc_root = tmp_path / "proc"
    pid_dir = proc_root / "123"
    pid_dir.mkdir(parents=True)
    stat_fields_after_comm = [
        "S",
        "1",
        "2",
        "3",
        "4",
        "5",
        "6",
        "7",
        "8",
        "9",
        "10",
        "11",
        "12",
        "13",
        "14",
        "15",
        "16",
        "17",
        "18",
        "250",
    ]
    (pid_dir / "stat").write_text(
        f"123 (name with spaces) {' '.join(stat_fields_after_comm)}",
        encoding="utf-8",
    )
    (proc_root / "stat").write_text(
        "cpu  1 2 3 4\nbtime 1700000000\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(ownership_module.sys, "platform", "linux")
    monkeypatch.setattr(ownership_module, "_PROC_ROOT", proc_root)
    monkeypatch.setattr(ownership_module.os, "sysconf", lambda name: 100)

    assert get_process_start_time(123) == 1700000002.5


def test_get_process_start_time_linux_handles_comm_with_parens(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    proc_root = tmp_path / "proc"
    pid_dir = proc_root / "123"
    pid_dir.mkdir(parents=True)
    fields = ["S", *[str(index) for index in range(1, 19)], "300"]
    (pid_dir / "stat").write_text(
        f"123 (name ) with parens) {' '.join(fields)}",
        encoding="utf-8",
    )
    (proc_root / "stat").write_text("btime 1700000000\n", encoding="utf-8")
    monkeypatch.setattr(ownership_module.sys, "platform", "linux")
    monkeypatch.setattr(ownership_module, "_PROC_ROOT", proc_root)
    monkeypatch.setattr(ownership_module.os, "sysconf", lambda name: 100)

    assert get_process_start_time(123) == 1700000003.0


def test_get_process_start_time_macos_reads_lstart(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class Result:
        stdout = "Thu Jun 11 12:34:56 2026\n"

    def fake_run(
        args: list[str],
        *,
        capture_output: bool,
        check: bool,
        env: dict[str, str],
        text: bool,
    ) -> Result:
        assert args == ["ps", "-o", "lstart=", "-p", "123"]
        assert capture_output is True
        assert check is False
        assert env["LC_ALL"] == "C"
        assert text is True
        return Result()

    monkeypatch.setattr(ownership_module.sys, "platform", "darwin")
    monkeypatch.setattr(ownership_module.subprocess, "run", fake_run)

    expected = datetime.strptime(
        "Thu Jun 11 12:34:56 2026", "%a %b %d %H:%M:%S %Y"
    ).timestamp()
    assert get_process_start_time(123) == expected


def test_get_process_start_time_macos_empty_output_is_none(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class Result:
        stdout = "\n"

    monkeypatch.setattr(ownership_module.sys, "platform", "darwin")
    monkeypatch.setattr(
        ownership_module.subprocess,
        "run",
        lambda *args, **kwargs: Result(),
    )

    assert get_process_start_time(123) is None


def test_generate_owner_id_uses_env_when_set(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("STACKCHAN_OWNER_ID", "custom-id")
    assert generate_owner_id() == "custom-id"


def test_generate_owner_id_falls_back_to_uuid(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.delenv("STACKCHAN_OWNER_ID", raising=False)
    label = generate_owner_id()
    assert label.startswith("stackchan-mcp-")
    assert len(label) == len("stackchan-mcp-") + 8
