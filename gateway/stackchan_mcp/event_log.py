"""Event log writer for firmware-originated stackchan events.

When the JSONL notification path is enabled, the gateway appends each
successfully-validated ``stackchan-event`` frame to a JSONL file (default
``~/.claude/stackchan-events.jsonl``) so downstream host integrations can
read events between the firmware reaction and the next conversational
turn. MCP notification paths are configured independently.

The log file path is overridable via ``STACKCHAN_EVENTS_PATH`` or an
explicit caller-provided path. Entries
whose ``ts_unix`` is older than ``RETENTION_DAYS`` are pruned exactly
once on gateway startup via :func:`rotate_old_entries`. Long-running
gateways are not re-rotated mid-flight; downstream readers are expected
to filter by ``ts_unix`` themselves, and any disk-growth concern over
multi-day uptimes is tracked as a separate follow-up.

All persistence failures (``PermissionError``, ``OSError``, malformed
lines, missing parent directory, etc.) are caught and logged at WARNING
level. The MCP notification path must never be broken by event log
persistence issues; callers should treat this helper as fire-and-forget.
"""

from __future__ import annotations

import json
import logging
import os
import time
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Final

logger = logging.getLogger(__name__)

DEFAULT_LOG_PATH: Final[Path] = Path.home() / ".claude" / "stackchan-events.jsonl"
PATH_ENV_VAR: Final[str] = "STACKCHAN_EVENTS_PATH"
RETENTION_DAYS: Final[int] = 7
_RETENTION_SECONDS: Final[int] = RETENTION_DAYS * 24 * 60 * 60


def resolve_log_path() -> Path:
    """Return the active event log path.

    Honors the ``STACKCHAN_EVENTS_PATH`` environment variable when set
    (``~`` is expanded), otherwise falls back to
    ``~/.claude/stackchan-events.jsonl``.
    """
    override = os.environ.get(PATH_ENV_VAR)
    if override:
        return Path(override).expanduser()
    return DEFAULT_LOG_PATH


def log_event(
    event_type: str,
    subtype: str,
    duration_ms: int,
    ts: int,
    session_id: str,
    *,
    action: str | None = None,
    path: Path | None = None,
    ts_unix: float | None = None,
) -> None:
    """Append a single stackchan event to the JSONL log.

    Parameters
    ----------
    event_type, subtype, duration_ms, ts, session_id
        Already-validated fields from the firmware-emitted
        ``stackchan-event`` WebSocket frame. ``ts`` is firmware uptime
        in milliseconds (monotonic); ``ts_unix`` is the wall-clock
        moment the gateway recorded the event and is what hook
        consumers should use for ``"how long ago"`` calculations.
    action
        Optional human-axis avatar action to include in the JSONL payload.
    path
        Optional resolved log path from notify.yml. When omitted, the legacy
        ``STACKCHAN_EVENTS_PATH`` / default path resolution is used.
    ts_unix
        Optional override for the wall-clock timestamp. Defaults to
        ``time.time()`` at append time. Exposed for tests.

    Errors are logged at WARNING and swallowed; the MCP notification
    path continues regardless of disk outcome.
    """
    if ts_unix is None:
        ts_unix = time.time()

    if path is None:
        path = resolve_log_path()
    line = {
        "event_type": event_type,
        "subtype": subtype,
        "duration_ms": duration_ms,
        "ts": ts,
        "ts_unix": ts_unix,
        "session_id": session_id,
    }
    if action is not None:
        line["action"] = action
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(line, ensure_ascii=False) + "\n")
            f.flush()
    except (OSError, PermissionError) as exc:
        logger.warning(
            "Failed to append stackchan-event log line to %s: %s",
            path,
            exc,
        )


def rotate_old_entries(
    *,
    path: Path | None = None,
    now_unix: float | None = None,
) -> None:
    """Prune log entries older than ``RETENTION_DAYS`` from the log file.

    Intended to be called exactly once at gateway startup. Reads every
    line, keeps the ones whose ``ts_unix`` is within the retention
    window, and atomically replaces the original file via
    ``os.replace`` on a same-directory temporary file. Malformed lines
    and lines without a usable ``ts_unix`` are dropped.

    A missing log file is a no-op. Any disk or permission error during
    rotation is logged at WARNING and swallowed so a broken log file
    cannot prevent the gateway from starting up.
    """
    if path is None:
        path = resolve_log_path()
    if not path.exists():
        return
    if now_unix is None:
        now_unix = time.time()
    cutoff = now_unix - _RETENTION_SECONDS

    kept: list[str] = []
    try:
        with path.open("r", encoding="utf-8") as f:
            for raw in f:
                stripped = raw.strip()
                if not stripped:
                    continue
                try:
                    obj = json.loads(stripped)
                except json.JSONDecodeError:
                    logger.debug(
                        "Dropping malformed event log line during rotation: %s",
                        stripped[:120],
                    )
                    continue
                if not isinstance(obj, dict):
                    continue
                ts_unix = obj.get("ts_unix")
                if isinstance(ts_unix, bool) or not isinstance(ts_unix, (int, float)):
                    continue
                if ts_unix >= cutoff:
                    kept.append(stripped + "\n")
    except (OSError, PermissionError) as exc:
        logger.warning(
            "Failed to read stackchan-event log %s for rotation: %s",
            path,
            exc,
        )
        return

    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            delete=False,
            dir=str(path.parent),
            prefix=path.name + ".",
            suffix=".tmp",
        ) as tmp:
            tmp.writelines(kept)
            tmp.flush()
            tmp_path = Path(tmp.name)
        os.replace(tmp_path, path)
    except (OSError, PermissionError) as exc:
        logger.warning(
            "Failed to atomically rotate stackchan-event log %s: %s",
            path,
            exc,
        )
