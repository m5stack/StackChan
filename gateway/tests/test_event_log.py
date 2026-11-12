"""Tests for ``stackchan_mcp.event_log`` JSONL writer + rotation helper."""

import json
import logging
import os
from pathlib import Path

from stackchan_mcp import event_log
from stackchan_mcp.event_log import (
    DEFAULT_LOG_PATH,
    PATH_ENV_VAR,
    RETENTION_DAYS,
    log_event,
    resolve_log_path,
    rotate_old_entries,
)


# --- resolve_log_path -------------------------------------------------------


def test_resolve_log_path_returns_default_when_env_unset(monkeypatch):
    monkeypatch.delenv(PATH_ENV_VAR, raising=False)
    assert resolve_log_path() == DEFAULT_LOG_PATH


def test_resolve_log_path_honors_env_override(monkeypatch, tmp_path):
    override = tmp_path / "custom-events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(override))
    assert resolve_log_path() == override


def test_resolve_log_path_expands_tilde(monkeypatch):
    monkeypatch.setenv(PATH_ENV_VAR, "~/custom/events.jsonl")
    expected = Path.home() / "custom" / "events.jsonl"
    assert resolve_log_path() == expected


# --- log_event happy path ---------------------------------------------------


def test_log_event_appends_line_to_jsonl(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    log_event(
        event_type="touch",
        subtype="tap",
        duration_ms=350,
        ts=123456,
        session_id="session-1",
        ts_unix=1717000000.5,
    )

    raw = path.read_text(encoding="utf-8")
    assert raw.endswith("\n")
    entry = json.loads(raw.splitlines()[-1])
    assert entry == {
        "event_type": "touch",
        "subtype": "tap",
        "duration_ms": 350,
        "ts": 123456,
        "ts_unix": 1717000000.5,
        "session_id": "session-1",
    }


def test_log_event_accepts_action_and_explicit_path(tmp_path):
    path = tmp_path / "configured-events.jsonl"

    log_event(
        event_type="touch",
        subtype="tap",
        duration_ms=350,
        ts=123456,
        session_id="session-1",
        action="head_pat",
        path=path,
        ts_unix=1717000000.5,
    )

    entry = json.loads(path.read_text(encoding="utf-8").splitlines()[-1])
    assert entry == {
        "event_type": "touch",
        "subtype": "tap",
        "duration_ms": 350,
        "ts": 123456,
        "ts_unix": 1717000000.5,
        "session_id": "session-1",
        "action": "head_pat",
    }


def test_log_event_appends_multiple_lines_in_order(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    log_event("touch", "tap", 100, 1, "s", ts_unix=1.0)
    log_event("touch", "stroke", 1200, 2, "s", ts_unix=2.0)
    log_event("touch", "tap", 200, 3, "s", ts_unix=3.0)

    lines = path.read_text(encoding="utf-8").splitlines()
    assert len(lines) == 3
    assert json.loads(lines[0])["ts_unix"] == 1.0
    assert json.loads(lines[1])["subtype"] == "stroke"
    assert json.loads(lines[2])["duration_ms"] == 200


def test_log_event_creates_missing_parent_directory(monkeypatch, tmp_path):
    path = tmp_path / "nested" / "deep" / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    log_event("touch", "tap", 100, 1, "s", ts_unix=1.0)

    assert path.exists()
    assert path.parent.exists()


def test_log_event_defaults_ts_unix_to_now(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    fixed_now = 1717000100.25
    monkeypatch.setattr(event_log.time, "time", lambda: fixed_now)

    log_event("touch", "tap", 100, 1, "s")

    entry = json.loads(path.read_text(encoding="utf-8").splitlines()[-1])
    assert entry["ts_unix"] == fixed_now


# --- log_event error swallowing --------------------------------------------


def test_log_event_swallows_oserror_and_logs_warning(
    monkeypatch, tmp_path, caplog
):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    def boom(*args, **kwargs):
        raise OSError("simulated-disk-full")

    monkeypatch.setattr(Path, "open", boom)

    with caplog.at_level(logging.WARNING):
        log_event("touch", "tap", 100, 1, "s", ts_unix=1.0)

    assert "Failed to append stackchan-event log line" in caplog.text


# --- rotate_old_entries ----------------------------------------------------


def test_rotate_missing_file_is_noop(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    rotate_old_entries(now_unix=1000.0)

    assert not path.exists()


def test_rotate_keeps_recent_drops_stale(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    now = 1717000000.0
    retention_secs = RETENTION_DAYS * 24 * 60 * 60
    stale_ts = now - retention_secs - 10
    fresh_ts = now - 60

    path.write_text(
        json.dumps(
            {
                "event_type": "touch",
                "subtype": "tap",
                "duration_ms": 100,
                "ts": 1,
                "ts_unix": stale_ts,
                "session_id": "old",
            }
        )
        + "\n"
        + json.dumps(
            {
                "event_type": "touch",
                "subtype": "stroke",
                "duration_ms": 1200,
                "ts": 2,
                "ts_unix": fresh_ts,
                "session_id": "new",
            }
        )
        + "\n",
        encoding="utf-8",
    )

    rotate_old_entries(now_unix=now)

    surviving = [
        json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
    ]
    assert len(surviving) == 1
    assert surviving[0]["session_id"] == "new"


def test_rotate_keeps_entries_exactly_at_cutoff(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    now = 1717000000.0
    retention_secs = RETENTION_DAYS * 24 * 60 * 60
    boundary_ts = now - retention_secs

    path.write_text(
        json.dumps(
            {
                "event_type": "touch",
                "subtype": "tap",
                "duration_ms": 100,
                "ts": 1,
                "ts_unix": boundary_ts,
                "session_id": "boundary",
            }
        )
        + "\n",
        encoding="utf-8",
    )

    rotate_old_entries(now_unix=now)

    surviving = [
        json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
    ]
    assert len(surviving) == 1
    assert surviving[0]["session_id"] == "boundary"


def test_rotate_drops_malformed_lines(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    now = 1717000000.0
    valid_recent = json.dumps(
        {
            "event_type": "touch",
            "subtype": "tap",
            "duration_ms": 100,
            "ts": 1,
            "ts_unix": now - 60,
            "session_id": "ok",
        }
    )
    path.write_text(
        "not-json\n"
        + valid_recent
        + "\n"
        + '{"ts_unix": "not-a-number"}\n'
        + "{}\n"
        + "[]\n"
        + "\n",
        encoding="utf-8",
    )

    rotate_old_entries(now_unix=now)

    surviving = [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line
    ]
    assert len(surviving) == 1
    assert surviving[0]["session_id"] == "ok"


def test_rotate_treats_bool_ts_unix_as_invalid(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    now = 1717000000.0
    path.write_text(
        json.dumps(
            {
                "event_type": "touch",
                "subtype": "tap",
                "duration_ms": 100,
                "ts": 1,
                "ts_unix": True,
                "session_id": "bool",
            }
        )
        + "\n"
        + json.dumps(
            {
                "event_type": "touch",
                "subtype": "tap",
                "duration_ms": 100,
                "ts": 2,
                "ts_unix": now - 60,
                "session_id": "ok",
            }
        )
        + "\n",
        encoding="utf-8",
    )

    rotate_old_entries(now_unix=now)

    surviving = [
        json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
    ]
    assert len(surviving) == 1
    assert surviving[0]["session_id"] == "ok"


def test_rotate_replaces_via_os_replace_atomically(monkeypatch, tmp_path):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))

    now = 1717000000.0
    path.write_text(
        json.dumps(
            {
                "event_type": "touch",
                "subtype": "tap",
                "duration_ms": 100,
                "ts": 1,
                "ts_unix": now - 60,
                "session_id": "keep",
            }
        )
        + "\n",
        encoding="utf-8",
    )

    calls = []
    original_replace = os.replace

    def tracking_replace(src, dst):
        calls.append((str(src), str(dst)))
        original_replace(src, dst)

    monkeypatch.setattr(event_log.os, "replace", tracking_replace)

    rotate_old_entries(now_unix=now)

    assert len(calls) == 1
    assert calls[0][1] == str(path)
    surviving = path.read_text(encoding="utf-8").splitlines()
    assert len(surviving) == 1


def test_rotate_swallows_read_error_and_logs_warning(
    monkeypatch, tmp_path, caplog
):
    path = tmp_path / "events.jsonl"
    monkeypatch.setenv(PATH_ENV_VAR, str(path))
    path.write_text("{}\n", encoding="utf-8")

    def boom_open(*args, **kwargs):
        raise OSError("read-permission-denied")

    monkeypatch.setattr(Path, "open", boom_open)

    with caplog.at_level(logging.WARNING):
        rotate_old_entries(now_unix=1000.0)

    assert "Failed to read stackchan-event log" in caplog.text
