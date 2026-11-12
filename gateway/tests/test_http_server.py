from __future__ import annotations

import asyncio
import contextlib
import json
from collections.abc import AsyncIterator

import httpx
import pytest
from mcp.server.streamable_http import MCP_SESSION_ID_HEADER
from mcp.types import ErrorData, TextContent

from stackchan_mcp.http_server import (
    AUTH_FAILURE_MESSAGE,
    BYPASS_TOOLS,
    DISCONNECTED_DEVICE_PAYLOAD,
    HOST_FAILURE_MESSAGE,
    ORIGIN_FAILURE_MESSAGE,
    MCP_HTTP_ALLOWED_HOSTS_ENV,
    build_app,
    make_dispatch_fn,
)
from stackchan_mcp.queue import CommandQueue, QueueFull, QueueItem, build_queue_full_error


class FakeESP32:
    def __init__(self, *, connected: bool = True) -> None:
        self.device_connected = connected
        self.calls: list[tuple[str, dict]] = []

    def get_status(self) -> dict:
        return {
            "connected": self.device_connected,
            "device": "fake-stackchan",
        }

    async def call_tool(
        self, name: str, arguments: dict, *, device_id: str | None = None
    ) -> tuple[dict, None]:
        self.calls.append((name, arguments))
        return {
            "content": [
                {
                    "type": "text",
                    "text": json.dumps({"name": name, "arguments": arguments}),
                }
            ],
        }, None


class FakeGateway:
    def __init__(self, *, connected: bool = True) -> None:
        self.esp32 = FakeESP32(connected=connected)


@contextlib.asynccontextmanager
async def _client(app, *, base_url: str = "http://127.0.0.1:8767") -> AsyncIterator[httpx.AsyncClient]:
    async with app.router.lifespan_context(app):
        transport = httpx.ASGITransport(app=app)
        async with httpx.AsyncClient(transport=transport, base_url=base_url) as client:
            yield client


def _headers(session_id: str | None = None, token: str | None = None) -> dict[str, str]:
    headers = {"accept": "application/json"}
    if session_id is not None:
        headers[MCP_SESSION_ID_HEADER] = session_id
    if token is not None:
        headers["authorization"] = f"Bearer {token}"
    return headers


async def _initialize(client: httpx.AsyncClient, *, token: str | None = None, request_id: int = 1) -> str:
    response = await client.post(
        "/mcp",
        json={
            "jsonrpc": "2.0",
            "id": request_id,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        },
        headers=_headers(token=token),
    )
    assert response.status_code == 200
    session_id = response.headers[MCP_SESSION_ID_HEADER]
    initialized = await client.post(
        "/mcp",
        json={"jsonrpc": "2.0", "method": "notifications/initialized"},
        headers=_headers(session_id, token),
    )
    assert initialized.status_code == 202
    return session_id


async def _call_tool(
    client: httpx.AsyncClient,
    *,
    session_id: str,
    name: str,
    arguments: dict | None = None,
    request_id: int | str = 2,
    token: str | None = None,
) -> httpx.Response:
    return await client.post(
        "/mcp",
        json={
            "jsonrpc": "2.0",
            "id": request_id,
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments or {}},
        },
        headers=_headers(session_id, token),
    )


async def _wait_for_queue_depth(queue: CommandQueue, depth: int) -> None:
    for _ in range(50):
        if queue.depth == depth:
            return
        await asyncio.sleep(0.01)
    raise AssertionError(f"queue depth did not reach {depth}")


class GatedCommandQueue(CommandQueue):
    def __init__(self, capacity: int) -> None:
        super().__init__(capacity=capacity)
        self.allow_get = asyncio.Event()
        self.get_started = asyncio.Event()
        self.enqueued_task: asyncio.Task | None = None
        self.last_enqueued_item: QueueItem | None = None

    def enqueue(self, item: QueueItem) -> None:
        task = asyncio.current_task()
        self.enqueued_task = task if isinstance(task, asyncio.Task) else None
        self.last_enqueued_item = item
        super().enqueue(item)

    async def get(self) -> QueueItem:
        self.get_started.set()
        await self.allow_get.wait()
        return await super().get()


@pytest.mark.asyncio
async def test_queue_ordering_fifo_completion() -> None:
    queue = CommandQueue(capacity=3)
    observed: list[str] = []
    loop = asyncio.get_running_loop()
    futures = [loop.create_future() for _ in range(3)]

    for index, future in enumerate(futures):
        queue.enqueue(
            QueueItem(
                correlation_id=f"item-{index}",
                client_session_id=None,
                client_request_id=index,
                tool_name=f"tool-{index}",
                arguments={},
                response_future=future,
                enqueued_at=0.0,
            )
        )

    async def dispatch(item: QueueItem) -> str:
        observed.append(item.tool_name)
        return item.tool_name

    dispatcher = asyncio.create_task(queue.run_dispatcher(dispatch))
    try:
        results = await asyncio.gather(*futures)
    finally:
        dispatcher.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await dispatcher

    assert observed == ["tool-0", "tool-1", "tool-2"]
    assert results == observed


@pytest.mark.asyncio
async def test_queue_full_returns_jsonrpc_error_response() -> None:
    queue = CommandQueue(capacity=1)
    app = build_app(
        queue,
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
    )

    async with _client(app) as client:
        session_id = await _initialize(client)
        first = asyncio.create_task(
            _call_tool(client, session_id=session_id, name="get_device_info", request_id=10)
        )
        await _wait_for_queue_depth(queue, 1)
        second = await _call_tool(
            client,
            session_id=session_id,
            name="get_head_angles",
            request_id=11,
        )
        first.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await first

    assert second.status_code == 200
    payload = second.json()
    assert payload["id"] == 11
    assert payload["error"] == build_queue_full_error(1)


@pytest.mark.asyncio
async def test_cancelled_client_item_is_not_dispatched() -> None:
    queue = GatedCommandQueue(capacity=2)
    dispatched: list[str] = []

    async def dispatch(item: QueueItem):
        dispatched.append(item.tool_name)
        return [TextContent(type="text", text=json.dumps({"ok": True}))]

    app = build_app(
        queue,
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        dispatch_fn=dispatch,
    )

    async with _client(app) as client:
        session_id = await _initialize(client)
        call_task = asyncio.create_task(
            _call_tool(
                client,
                session_id=session_id,
                name="get_device_info",
            )
        )
        await _wait_for_queue_depth(queue, 1)

        assert queue.enqueued_task is not None
        queue.enqueued_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await call_task
        assert queue.last_enqueued_item is not None
        assert queue.last_enqueued_item.response_future.cancelled()

        queue.allow_get.set()
        await _wait_for_queue_depth(queue, 0)
        await asyncio.sleep(0.01)

    assert dispatched == []


@pytest.mark.asyncio
async def test_lifespan_shutdown_drains_pending_queue_items() -> None:
    queue = GatedCommandQueue(capacity=3)
    dispatched: list[str] = []

    async def dispatch(item: QueueItem):
        dispatched.append(item.tool_name)
        return [TextContent(type="text", text=json.dumps({"ok": True}))]

    app = build_app(
        queue,
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        dispatch_fn=dispatch,
    )

    loop = asyncio.get_running_loop()
    futures = [loop.create_future() for _ in range(2)]

    async with app.router.lifespan_context(app):
        for index, future in enumerate(futures):
            queue.enqueue(
                QueueItem(
                    correlation_id=f"pending-{index}",
                    client_session_id=None,
                    client_request_id=index,
                    tool_name=f"tool-{index}",
                    arguments={},
                    response_future=future,
                    enqueued_at=0.0,
                )
            )
        await _wait_for_queue_depth(queue, 2)

    assert queue.depth == 0
    assert dispatched == []
    for future in futures:
        assert future.done()
        result = future.result()
        assert isinstance(result, ErrorData)
        assert result.code == -32000
        assert result.message == "stackchan MCP HTTP server is shutting down"
        assert result.data == {"reason": "server_shutdown"}


@pytest.mark.asyncio
async def test_auth_rejection_and_successful_bearer_reaches_dispatcher() -> None:
    queue = CommandQueue(capacity=4)
    dispatched: list[str] = []

    async def dispatch(item: QueueItem):
        dispatched.append(item.tool_name)
        return [TextContent(type="text", text=json.dumps({"ok": True}))]

    app = build_app(
        queue,
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        token="secret",
        dispatch_fn=dispatch,
    )

    async with _client(app) as client:
        missing = await client.post(
            "/mcp",
            json={"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
            headers=_headers(),
        )
        wrong = await client.post(
            "/mcp",
            json={"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
            headers=_headers(token="wrong"),
        )
        session_id = await _initialize(client, token="secret")
        ok = await _call_tool(
            client,
            session_id=session_id,
            token="secret",
            name="get_device_info",
        )

    assert missing.status_code == 401
    assert missing.text == AUTH_FAILURE_MESSAGE
    assert wrong.status_code == 401
    assert ok.status_code == 200
    assert dispatched == ["get_device_info"]


@pytest.mark.asyncio
async def test_host_and_origin_rebinding_guards_return_403() -> None:
    app = build_app(
        CommandQueue(capacity=2),
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
    )

    async with _client(app) as client:
        bad_host = await client.get(
            "/healthz",
            headers={"host": "evil.example:8767"},
        )
        bad_origin = await client.get(
            "/healthz",
            headers={"origin": "http://evil.example:8767"},
        )

    assert bad_host.status_code == 403
    assert bad_host.text == HOST_FAILURE_MESSAGE
    assert bad_origin.status_code == 403
    assert bad_origin.text == ORIGIN_FAILURE_MESSAGE


@pytest.mark.asyncio
async def test_wildcard_bind_allows_loopback_and_configured_hosts(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv(
        MCP_HTTP_ALLOWED_HOSTS_ENV,
        "192.168.1.10, https://stackchan.example.test:9443",
    )
    app = build_app(
        CommandQueue(capacity=2),
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="0.0.0.0",
        port=8767,
    )

    async with _client(app) as client:
        loopback = await client.get("/healthz", headers={"host": "127.0.0.1:8767"})
        lan = await client.get("/healthz", headers={"host": "192.168.1.10:8767"})
        origin = await client.get(
            "/healthz",
            headers={
                "host": "stackchan.example.test:9443",
                "origin": "https://stackchan.example.test:9443",
            },
        )

    assert loopback.status_code == 200
    assert lan.status_code == 200
    assert origin.status_code == 200


@pytest.mark.asyncio
async def test_response_correlation_for_two_concurrent_clients() -> None:
    queue = CommandQueue(capacity=4)

    async def dispatch(item: QueueItem):
        await asyncio.sleep(0.01)
        return [
            TextContent(
                type="text",
                text=json.dumps(
                    {
                        "client_request_id": item.client_request_id,
                        "tool_name": item.tool_name,
                    }
                ),
            )
        ]

    app = build_app(
        queue,
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        dispatch_fn=dispatch,
    )

    async with _client(app) as client:
        session_a = await _initialize(client, request_id=1)
        session_b = await _initialize(client, request_id=2)
        response_a, response_b = await asyncio.gather(
            _call_tool(
                client,
                session_id=session_a,
                name="get_device_info",
                request_id="client-a",
            ),
            _call_tool(
                client,
                session_id=session_b,
                name="get_head_angles",
                request_id="client-b",
            ),
        )

    payload_a = response_a.json()
    payload_b = response_b.json()
    assert payload_a["id"] == "client-a"
    assert payload_b["id"] == "client-b"
    body_a = json.loads(payload_a["result"]["content"][0]["text"])
    body_b = json.loads(payload_b["result"]["content"][0]["text"])
    assert body_a["client_request_id"] == "client-a"
    assert body_b["client_request_id"] == "client-b"


def test_bypass_tools_include_status_and_follow_pose_stream() -> None:
    assert "get_status" in BYPASS_TOOLS
    assert "stackchan_follow_pose_stream" in BYPASS_TOOLS


@pytest.mark.asyncio
async def test_bypass_tool_get_status_does_not_enter_dispatcher() -> None:
    assert BYPASS_TOOLS == frozenset(
        {
            "get_status",
            "agent_update_snapshot",
            "agent_get_dashboard",
            "stackchan_follow_pose_stream",
            "list_devices",
            "set_default_device",
        }
    )
    queue = CommandQueue(capacity=2)

    async def dispatch(_item: QueueItem):
        raise AssertionError("get_status must bypass the command queue")

    app = build_app(
        queue,
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        dispatch_fn=dispatch,
    )

    async with _client(app) as client:
        session_id = await _initialize(client)
        response = await _call_tool(client, session_id=session_id, name="get_status")

    payload = response.json()
    status = json.loads(payload["result"]["content"][0]["text"])
    assert status["connected"] is True
    assert queue.depth == 0


@pytest.mark.asyncio
async def test_dispatcher_returns_stdio_disconnect_payload_as_tool_result() -> None:
    queue = CommandQueue(capacity=2)
    gateway = FakeGateway(connected=False)
    app = build_app(
        queue,
        gateway=gateway,
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        dispatch_fn=make_dispatch_fn(gateway),
    )

    async with _client(app) as client:
        session_id = await _initialize(client)
        response = await _call_tool(
            client,
            session_id=session_id,
            name="get_device_info",
        )

    payload = response.json()
    assert "error" not in payload
    result_text = payload["result"]["content"][0]["text"]
    assert json.loads(result_text) == DISCONNECTED_DEVICE_PAYLOAD


@pytest.mark.asyncio
async def test_healthz_is_liveness_only_and_status_requires_auth_for_details() -> None:
    queue = CommandQueue(capacity=2)
    app = build_app(
        queue,
        gateway=FakeGateway(),
        owner_id="owner-test",
        host="127.0.0.1",
        port=8767,
        token="secret",
    )

    async with _client(app) as client:
        health = await client.get("/healthz")
        unauthenticated_status = await client.get("/status")
        status = await client.get("/status", headers=_headers(token="secret"))

    assert health.status_code == 200
    assert health.json() == {"ok": True}
    assert set(health.json()) == {"ok"}
    assert unauthenticated_status.status_code == 401

    status_payload = status.json()
    assert status.status_code == 200
    assert status_payload["connected"] is True
    assert status_payload["esp32_connected"] is True
    assert status_payload["queue_depth"] == 0
    assert status_payload["queue_capacity"] == 2
    assert status_payload["owner_id"] == "owner-test"
    assert status_payload["connected_clients"] == 0


@pytest.mark.asyncio
async def test_command_queue_raises_queue_full_directly() -> None:
    queue = CommandQueue(capacity=1)
    loop = asyncio.get_running_loop()
    queue.enqueue(
        QueueItem(
            correlation_id="first",
            client_session_id=None,
            client_request_id=1,
            tool_name="get_device_info",
            arguments={},
            response_future=loop.create_future(),
            enqueued_at=0.0,
        )
    )
    with pytest.raises(QueueFull):
        queue.enqueue(
            QueueItem(
                correlation_id="second",
                client_session_id=None,
                client_request_id=2,
                tool_name="get_head_angles",
                arguments={},
                response_future=loop.create_future(),
                enqueued_at=0.0,
            )
        )
