"""Run privacy-limited local agent event bridges into the shared MCP gateway."""

from __future__ import annotations

import asyncio
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

from . import codex_desktop_session_id, normalize_claude_hook, normalize_codex_record
from .claude_transcript import ClaudeTranscript
from .codex_plan import read_plan, write_plan


def _endpoint() -> tuple[str, dict[str, str]]:
    url = os.environ.get("STACKCHAN_MCP_URL", "")
    token = os.environ.get("STACKCHAN_TOKEN", "")
    if not url:
        raise RuntimeError("请设置 STACKCHAN_MCP_URL，例如 http://127.0.0.1:8767/mcp")
    if not token:
        raise RuntimeError("请设置 STACKCHAN_TOKEN")
    return url, {"Authorization": f"Bearer {token}"}


async def _publish(snapshot: dict[str, Any]) -> None:
    url, headers = _endpoint()
    async with streamablehttp_client(url, headers=headers) as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.call_tool("agent_update_snapshot", _heartbeat(snapshot))
            if result.isError:
                raise RuntimeError("MCP 网关拒绝状态更新")
            for content in result.content:
                if content.type == "text":
                    payload = json.loads(content.text)
                    if payload.get("error") or payload.get("accepted") is False:
                        raise RuntimeError("MCP 网关未接受状态更新")


def _heartbeat(snapshot: dict[str, Any]) -> dict[str, Any]:
    """Every heartbeat must advance the gateway's monotonic sequence fence."""
    return {
        **snapshot,
        "sequence": max(time.time_ns(), snapshot["sequence"] + 1),
        "observed_at": datetime.now(timezone.utc).isoformat(),
    }


def _read_records(path: Path, offset: int) -> tuple[list[dict[str, Any]], int]:
    """Leave an incomplete final line for the next poll."""
    records = []
    with path.open("rb") as stream:
        stream.seek(offset)
        while line := stream.readline():
            if not line.endswith(b"\n"):
                break
            offset = stream.tell()
            try:
                record = json.loads(line)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            if isinstance(record, dict):
                records.append(record)
    return records, offset


def _claude_hook() -> None:
    event = json.load(sys.stdin)
    sequence = time.time_ns()
    snapshot = normalize_claude_hook(event, sequence=sequence)
    if snapshot is not None:
        asyncio.run(_publish(snapshot))


async def _watch_codex() -> None:
    root = Path(os.environ.get("CODEX_SESSIONS_DIR", Path.home() / ".codex/sessions"))
    seen: dict[str, tuple[tuple[int, int], int]] = {}
    session_ids: dict[str, str] = {}
    latest_snapshots: dict[str, tuple[int, dict[str, Any]]] = {}
    sequence = time.time_ns()
    published_versions: dict[str, tuple[int, int]] = {}
    last_heartbeat = 0.0
    next_attempt = 0.0
    retry_delay = 1.0
    active_source: str | None = None
    activation_id = ""
    while True:
        files = sorted(
            root.glob("**/*.jsonl"), key=lambda path: _mtime(path), reverse=True
        )[:32]
        for path in files:
            try:
                stat = path.stat()
            except OSError:
                continue
            signature = (stat.st_mtime_ns, stat.st_size)
            old_signature, offset = seen.get(str(path), ((0, 0), 0))
            if signature[1] < offset:
                offset = 0
            if signature == old_signature:
                continue
            try:
                records, offset = _read_records(path, offset)
            except OSError:
                continue
            # Do not cache the signature of a partial final record.
            seen[str(path)] = (signature if offset == stat.st_size else (0, 0), offset)
            session_id: str | None = session_ids.get(str(path))
            for record in records:
                payload = record.get("payload")
                if record.get("type") == "session_meta" and isinstance(payload, dict):
                    session_id = codex_desktop_session_id(record)
                    if session_id is not None:
                        session_ids[str(path)] = session_id
                    else:
                        session_ids.pop(str(path), None)
                    continue
                if session_id is None or not isinstance(record, dict):
                    continue
                sequence += 1
                snapshot = normalize_codex_record(
                    record, sequence=sequence, session_id=session_id
                )
                if snapshot is not None:
                    previous = latest_snapshots.get(session_id)
                    if previous and previous[0] > stat.st_mtime_ns:
                        continue
                    if previous and payload.get("type") not in {"turn_plan_updated", "plan_updated"}:
                        snapshot["tasks"] = previous[1]["tasks"]
                    latest_snapshots[session_id] = (stat.st_mtime_ns, snapshot)
        if latest_snapshots:
            session_id, (_, snapshot) = max(
                latest_snapshots.items(), key=lambda item: item[1][0]
            )
            now = time.monotonic()
            plan = read_plan(session_id)
            version = (snapshot["sequence"], plan["revision"] if plan else -1)
            if plan is not None:
                snapshot = {**snapshot, "tasks": plan["tasks"]}
                if snapshot["status"] == "working":
                    snapshot["stage"] = plan["stage"]
            if session_id != active_source:
                active_source = session_id
                # A thread may become active again after another thread.
                # Give that activation its own lease so retired updates remain fenced.
                activation_id = f"{session_id[:95]}:{time.time_ns()}"
                published_versions.pop(session_id, None)
            snapshot = {**snapshot, "session_id": activation_id}
            if now >= next_attempt and (version != published_versions.get(session_id) or now - last_heartbeat >= 15):
                try:
                    await _publish(snapshot)
                    published_versions[session_id] = version
                    last_heartbeat = now
                    retry_delay = 1.0
                    next_attempt = 0.0
                except Exception as exc:
                    print(f"Codex 状态暂未上报: {type(exc).__name__}", file=sys.stderr)
                    next_attempt = now + retry_delay
                    retry_delay = min(30.0, retry_delay * 2)
        await asyncio.sleep(0.5)


def _mtime(path: Path) -> int:
    try:
        return path.stat().st_mtime_ns
    except OSError:
        return 0


async def _watch_claude() -> None:
    root = Path(os.environ.get("CLAUDE_PROJECTS_DIR", Path.home() / ".claude/projects"))
    seen: dict[Path, tuple[tuple[int, int], int]] = {}
    parsers: dict[Path, ClaudeTranscript] = {}
    latest: dict[Path, tuple[int, dict[str, Any]]] = {}
    active_source: Path | None = None
    activation_id = ""
    published_sequence = -1
    last_heartbeat = 0.0
    next_attempt = 0.0
    retry_delay = 1.0
    while True:
        # Primary transcripts live directly inside project folders. Subagents
        # live deeper and must never take over the user's Claude column.
        files = sorted(root.glob("*/*.jsonl"), key=_mtime, reverse=True)[:32]
        for path in files:
            try:
                stat = path.stat()
                signature = (stat.st_mtime_ns, stat.st_size)
                previous, offset = seen.get(path, ((0, 0), 0))
                if stat.st_size < offset:
                    offset = 0
                    parsers.pop(path, None)
                    latest.pop(path, None)
                if signature == previous:
                    continue
                records, offset = _read_records(path, offset)
            except OSError:
                continue
            seen[path] = (signature if offset == stat.st_size else (0, 0), offset)
            parser = parsers.setdefault(path, ClaudeTranscript())
            for record in records:
                snapshot = parser.consume(record)
                if snapshot is not None:
                    latest[path] = (stat.st_mtime_ns, snapshot)
        if latest:
            source, (_, snapshot) = max(latest.items(), key=lambda item: item[1][0])
            now = time.monotonic()
            if source != active_source:
                active_source = source
                activation_id = f"{snapshot['session_id'][:95]}:{time.time_ns()}"
                published_sequence = -1
            snapshot = {**snapshot, "session_id": activation_id}
            if now >= next_attempt and (
                snapshot["sequence"] != published_sequence or now - last_heartbeat >= 15
            ):
                try:
                    await _publish(snapshot)
                    published_sequence = snapshot["sequence"]
                    last_heartbeat = now
                    retry_delay = 1.0
                    next_attempt = 0.0
                except Exception as exc:
                    print(f"Claude 状态暂未上报: {type(exc).__name__}", file=sys.stderr)
                    next_attempt = now + retry_delay
                    retry_delay = min(30.0, retry_delay * 2)
        await asyncio.sleep(0.5)


def main() -> None:
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "claude-hook":
        _claude_hook()
    elif mode == "codex-watch":
        asyncio.run(_watch_codex())
    elif mode == "claude-watch":
        asyncio.run(_watch_claude())
    elif mode == "codex-plan":
        payload = json.load(sys.stdin)
        session_id = payload.get("session_id") or os.environ.get("CODEX_THREAD_ID")
        write_plan(session_id, payload.get("plan"), payload.get("stage", "计划"))
        print("Codex 会话计划已保存，将由监视器通过 MCP 上报。")
    else:
        raise SystemExit("用法: python -m stackchan_mcp.agent_adapters.runner {claude-hook|codex-watch|claude-watch|codex-plan}")


if __name__ == "__main__":
    main()
