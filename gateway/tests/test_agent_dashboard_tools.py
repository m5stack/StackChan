from __future__ import annotations

import json
from datetime import UTC, datetime

import pytest
import httpx
from mcp.server.streamable_http import MCP_SESSION_ID_HEADER
from mcp.types import ListToolsRequest

import stackchan_mcp.stdio_server as stdio_server
from stackchan_mcp.agent_status import AgentSnapshot
from stackchan_mcp.gateway import Gateway
from stackchan_mcp.http_server import BYPASS_TOOLS, build_app, make_dispatch_fn
from stackchan_mcp.queue import CommandQueue
from stackchan_mcp.stdio_server import _dispatch_mcp_tool, create_server


def _snapshot(*, sequence: int = 1, text: str = "正在实现界面") -> dict:
    return {
        "agent": "codex",
        "session_id": "session-1",
        "sequence": sequence,
        "observed_at": datetime.now(UTC).isoformat(),
        "status": "working",
        "stage": "实现",
        "tasks": [{"id": "task-1", "text": text, "status": "in_progress"}],
    }


class FakeESP32:
    device_connected = True

    def __init__(self) -> None:
        self.calls: list[tuple[str, dict]] = []

    async def call_tool(self, name: str, arguments: dict, *, device_id=None):
        self.calls.append((name, arguments))
        return {"content": [{"type": "text", "text": "{}"}]}, None


@pytest.mark.asyncio
async def test_dashboard_tools_are_listed():
    server = create_server()
    result = await server.request_handlers[ListToolsRequest](
        ListToolsRequest(method="tools/list")
    )
    names = {tool.name for tool in result.root.tools}
    assert {
        "agent_update_snapshot",
        "agent_get_dashboard",
    } <= names
    assert {"agent_update_snapshot", "agent_get_dashboard"} <= BYPASS_TOOLS
    assert not ({"agent_show_dashboard", "agent_show_home"} & names)


@pytest.mark.asyncio
async def test_agent_update_and_get_are_local_and_keep_both_columns(monkeypatch):
    gateway = Gateway()
    fake = FakeESP32()
    fake.device_connected = False
    gateway.esp32 = fake
    monkeypatch.setattr(stdio_server, "get_gateway", lambda: gateway)

    updated = await _dispatch_mcp_tool("agent_update_snapshot", _snapshot(), gateway)
    assert json.loads(updated[0].text)["accepted"] is True
    assert fake.calls == []

    dashboard = json.loads(
        (await _dispatch_mcp_tool("agent_get_dashboard", {}, gateway))[0].text
    )
    assert [column["agent"] for column in dashboard["agents"]] == ["codex", "claude"]
    assert dashboard["agents"][0]["tasks"][0]["text"] == "正在实现界面"
    assert dashboard["agents"][1]["online"] is False
    assert fake.calls == []


@pytest.mark.asyncio
async def test_old_snapshot_is_not_accepted_or_published():
    gateway = Gateway()
    fake = FakeESP32()
    gateway.esp32 = fake
    await _dispatch_mcp_tool("agent_update_snapshot", _snapshot(sequence=2), gateway)

    result = await _dispatch_mcp_tool("agent_update_snapshot", _snapshot(sequence=1), gateway)

    assert json.loads(result[0].text)["accepted"] is False
    assert fake.calls == []


@pytest.mark.asyncio
@pytest.mark.parametrize("agent", ["codex", "claude"])
async def test_dashboard_sorts_active_tasks_first_and_reorders_after_updates(agent):
    gateway = Gateway()
    payload = {**_snapshot(), "agent": agent, "tasks": [
        {"id": "done", "text": "已完成", "status": "completed"},
        {"id": "pending-a", "text": "待办甲", "status": "pending"},
        {"id": "active-a", "text": "执行甲", "status": "in_progress"},
        {"id": "pending-b", "text": "待办乙", "status": "pending"},
        {"id": "active-b", "text": "执行乙", "status": "in_progress"},
    ]}
    await _dispatch_mcp_tool("agent_update_snapshot", payload.copy(), gateway)
    column = 0 if agent == "codex" else 1
    dashboard = await gateway.get_agent_dashboard()
    assert [t["id"] for t in dashboard["agents"][column]["tasks"]] == [
        "active-a", "active-b", "pending-a", "pending-b", "done",
    ]
    payload["sequence"] = 2
    payload["tasks"][2]["status"] = "completed"
    payload["tasks"][3]["status"] = "in_progress"
    await _dispatch_mcp_tool("agent_update_snapshot", payload.copy(), gateway)
    dashboard = await gateway.get_agent_dashboard()
    assert [t["id"] for t in dashboard["agents"][column]["tasks"]] == [
        "pending-b", "active-b", "pending-a", "done", "active-a",
    ]


@pytest.mark.asyncio
async def test_agent_dashboard_endpoint_returns_store_snapshot_and_requires_token():
    gateway = Gateway()
    await gateway.update_agent_snapshot(
        AgentSnapshot(
            agent="codex", session_id="session-1", sequence=1,
            observed_at=datetime.now(UTC), status="working", stage="实现",
            tasks=(),
        )
    )
    queue = CommandQueue(capacity=2)
    app = build_app(
        queue, gateway=gateway, owner_id="test", host="127.0.0.1", port=8767,
        token="test-token",
    )

    async with app.router.lifespan_context(app):
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://127.0.0.1:8767"
        ) as client:
            denied = await client.get("/agent-dashboard")
            wrong = await client.get(
                "/agent-dashboard", headers={"authorization": "Bearer wrong"}
            )
            response = await client.get(
                "/agent-dashboard", headers={"authorization": "Bearer test-token"}
            )

    assert denied.status_code == 401
    assert wrong.status_code == 401
    assert response.status_code == 200
    assert [row["agent"] for row in response.json()["agents"]] == ["codex", "claude"]
    assert response.json()["agents"][0]["stage"] == "实现"
    assert queue.depth == 0


@pytest.mark.asyncio
async def test_http_update_bypasses_device_queue_when_disconnected():
    gateway = Gateway()
    fake = FakeESP32()
    fake.device_connected = False
    gateway.esp32 = fake
    queue = CommandQueue(capacity=1)
    app = build_app(
        queue, gateway=gateway, owner_id="test", host="127.0.0.1", port=8767,
        dispatch_fn=make_dispatch_fn(gateway),
    )

    async with app.router.lifespan_context(app):
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://127.0.0.1:8767"
        ) as client:
            initialized = await client.post(
                "/mcp",
                json={
                    "jsonrpc": "2.0", "id": 1, "method": "initialize",
                    "params": {
                        "protocolVersion": "2025-06-18",
                        "capabilities": {},
                        "clientInfo": {"name": "pytest", "version": "0"},
                    },
                },
                headers={"accept": "application/json"},
            )
            session_id = initialized.headers[MCP_SESSION_ID_HEADER]
            await client.post(
                "/mcp", json={"jsonrpc": "2.0", "method": "notifications/initialized"},
                headers={"accept": "application/json", MCP_SESSION_ID_HEADER: session_id},
            )
            response = await client.post(
                "/mcp",
                json={
                    "jsonrpc": "2.0", "id": 2, "method": "tools/call",
                    "params": {"name": "agent_update_snapshot", "arguments": _snapshot()},
                },
                headers={"accept": "application/json", MCP_SESSION_ID_HEADER: session_id},
            )

    assert response.status_code == 200
    assert queue.depth == 0
    assert json.loads(response.json()["result"]["content"][0]["text"])["accepted"] is True
    assert fake.calls == []
