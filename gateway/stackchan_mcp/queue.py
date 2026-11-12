"""Bounded command queue for Issue #178 Phase B chunk 1.

This module follows the command-queue design in
``docs/178-http-transport-spike.md`` and intentionally stays independent from
the HTTP MCP server, MCP SDK objects, and ESP32 gateway wiring.
"""

from __future__ import annotations

import asyncio
import os
import time
import uuid
from collections.abc import Awaitable, Callable
from contextlib import suppress
from dataclasses import dataclass
from typing import Any

COMMAND_QUEUE_SIZE_ENV = "STACKCHAN_COMMAND_QUEUE_SIZE"
DEFAULT_COMMAND_QUEUE_CAPACITY = 32
QUEUE_FULL_ERROR_CODE = -32000
QUEUE_FULL_MESSAGE = "stackchan command queue is full"
QUEUE_FULL_RETRY_AFTER_MS = 250

DispatchFn = Callable[["QueueItem"], Awaitable[Any]]


@dataclass(frozen=True)
class QueueItem:
    """One ESP32-bound command and the future that receives its response."""

    correlation_id: str
    client_session_id: str | None
    client_request_id: int | str
    tool_name: str
    arguments: dict[str, Any]
    response_future: asyncio.Future[Any]
    enqueued_at: float


class QueueFull(Exception):
    """Raised by CommandQueue.enqueue when capacity is reached."""

    def __init__(self, queue_depth: int, capacity: int) -> None:
        self.queue_depth = queue_depth
        self.capacity = capacity
        super().__init__(
            f"{QUEUE_FULL_MESSAGE} (queue_depth={queue_depth}, capacity={capacity})"
        )


class CommandQueue:
    """Asyncio-backed bounded FIFO queue for serialized command dispatch."""

    def __init__(self, capacity: int | None = None) -> None:
        self._capacity = capacity if capacity is not None else _capacity_from_env()
        if self._capacity < 1:
            raise ValueError("command queue capacity must be at least 1")
        self._queue: asyncio.Queue[QueueItem] = asyncio.Queue(
            maxsize=self._capacity
        )

    @property
    def capacity(self) -> int:
        """Return the maximum number of queued commands."""
        return self._capacity

    @property
    def depth(self) -> int:
        """Return the current number of queued commands."""
        return self._queue.qsize()

    def enqueue(self, item: QueueItem) -> None:
        """Add an item without blocking, raising QueueFull on saturation."""
        try:
            self._queue.put_nowait(item)
        except asyncio.QueueFull as exc:
            raise QueueFull(self.depth, self.capacity) from exc

    async def get(self) -> QueueItem:
        """Await the next queued item in FIFO order."""
        return await self._queue.get()

    async def run_dispatcher(self, dispatch_fn: DispatchFn) -> None:
        """Dispatch commands one at a time and complete each response future.

        The injected dispatch function is awaited to completion before the next
        queue item is fetched. Exceptions from dispatch_fn are delivered to the
        item's response_future so the dispatcher loop can continue.
        """
        while True:
            item = await self.get()
            try:
                result = await dispatch_fn(item)
            except Exception as exc:
                if not item.response_future.done():
                    item.response_future.set_exception(exc)
            else:
                if not item.response_future.done():
                    item.response_future.set_result(result)
            finally:
                self._queue.task_done()


def build_queue_full_error(
    queue_depth: int,
    retry_after_ms: int = QUEUE_FULL_RETRY_AFTER_MS,
) -> dict[str, Any]:
    """Build the JSON-RPC inner error payload for queue saturation."""
    return {
        "code": QUEUE_FULL_ERROR_CODE,
        "message": QUEUE_FULL_MESSAGE,
        "data": {
            "queue_depth": queue_depth,
            "retry_after_ms": retry_after_ms,
        },
    }


def _capacity_from_env() -> int:
    raw_capacity = os.getenv(COMMAND_QUEUE_SIZE_ENV)
    if raw_capacity is None or raw_capacity == "":
        return DEFAULT_COMMAND_QUEUE_CAPACITY
    try:
        capacity = int(raw_capacity)
    except ValueError as exc:
        raise ValueError(
            f"{COMMAND_QUEUE_SIZE_ENV} must be an integer"
        ) from exc
    if capacity < 1:
        raise ValueError(f"{COMMAND_QUEUE_SIZE_ENV} must be at least 1")
    return capacity


def _make_smoke_item(
    response_future: asyncio.Future[Any],
    tool_name: str = "smoke.tool",
) -> QueueItem:
    return QueueItem(
        correlation_id=str(uuid.uuid4()),
        client_session_id="smoke-session",
        client_request_id=1,
        tool_name=tool_name,
        arguments={"value": "smoke"},
        response_future=response_future,
        enqueued_at=time.monotonic(),
    )


async def _run_smoke() -> None:
    queue = CommandQueue(capacity=1)
    response_future: asyncio.Future[Any] = asyncio.get_running_loop().create_future()
    queue.enqueue(_make_smoke_item(response_future))

    async def dispatch_fn(item: QueueItem) -> dict[str, Any]:
        return {
            "ok": True,
            "correlation_id": item.correlation_id,
            "tool_name": item.tool_name,
        }

    dispatcher = asyncio.create_task(queue.run_dispatcher(dispatch_fn))
    result = await asyncio.wait_for(response_future, timeout=1.0)
    assert result["ok"] is True
    assert result["tool_name"] == "smoke.tool"

    dispatcher.cancel()
    with suppress(asyncio.CancelledError):
        await dispatcher

    full_queue = CommandQueue(capacity=1)
    full_queue.enqueue(
        _make_smoke_item(asyncio.get_running_loop().create_future(), "full.first")
    )
    try:
        full_queue.enqueue(
            _make_smoke_item(
                asyncio.get_running_loop().create_future(),
                "full.second",
            )
        )
    except QueueFull as exc:
        assert exc.queue_depth == 1
        assert build_queue_full_error(exc.queue_depth)["code"] == -32000
    else:
        raise AssertionError("QueueFull was not raised")


if __name__ == "__main__":
    asyncio.run(_run_smoke())
    print("smoke: PASS")
