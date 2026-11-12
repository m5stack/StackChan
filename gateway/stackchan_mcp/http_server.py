"""Streamable HTTP MCP daemon wiring for the StackChan gateway."""

from __future__ import annotations

import asyncio
import contextlib
import ipaddress
import json
import os
import time
import uuid
from collections.abc import Awaitable, Callable
from typing import Any
from urllib.parse import urlparse

import jsonschema
from mcp.server.streamable_http import MCP_SESSION_ID_HEADER
from mcp.server.streamable_http_manager import StreamableHTTPSessionManager
from mcp.types import (
    CallToolRequest,
    CallToolResult,
    ErrorData,
    ImageContent,
    ServerResult,
    TextContent,
)
from starlette.applications import Starlette
from starlette.requests import Request
from starlette.responses import JSONResponse, PlainTextResponse
from starlette.routing import Route
from starlette.types import Receive, Scope, Send

from .notify_config import NotifyConfig
from .queue import CommandQueue, QueueFull, QueueItem, build_queue_full_error
from .stdio_server import _dispatch_mcp_tool, create_server

# Follower lifecycle operations must remain callable when the ESP32 is
# disconnected so HTTP clients can supervise the background task.
# Device registry tools must also work with zero devices connected, since
# showing an empty device list is part of their contract.
BYPASS_TOOLS = frozenset(
    {
        "get_status",
        "agent_update_snapshot",
        "agent_get_dashboard",
        "stackchan_follow_pose_stream",
        "list_devices",
        "set_default_device",
    }
)
MCP_HTTP_ALLOWED_HOSTS_ENV = "MCP_HTTP_ALLOWED_HOSTS"
AUTH_FAILURE_MESSAGE = "Unauthorized: missing or invalid bearer token"
HOST_FAILURE_MESSAGE = "Forbidden: invalid Host header"
ORIGIN_FAILURE_MESSAGE = "Forbidden: invalid Origin header"
NON_LOOPBACK_TOKEN_REQUIRED_MESSAGE = (
    "stackchan-mcp: refusing non-loopback MCP_HTTP_HOST without "
    "STACKCHAN_TOKEN or BEARER_TOKEN"
)
DISCONNECTED_DEVICE_PAYLOAD = {
    "error": "No ESP32 device connected. Please check the device."
}
SERVER_SHUTDOWN_ERROR_CODE = -32000
SERVER_SHUTDOWN_ERROR_MESSAGE = "stackchan MCP HTTP server is shutting down"

DispatchFn = Callable[[QueueItem], Awaitable[list[TextContent | ImageContent]]]


def get_configured_token() -> str | None:
    """Return the configured HTTP bearer token, if any."""
    return os.getenv("STACKCHAN_TOKEN") or os.getenv("BEARER_TOKEN") or None


def is_wildcard_bind_host(host: str) -> bool:
    """Return whether ``host`` binds all local interfaces."""
    normalized = host.strip().lower()
    return normalized in {"", "0.0.0.0", "::"}


def is_loopback_bind_host(host: str) -> bool:
    """Return whether ``host`` is a loopback-only bind target."""
    normalized = host.strip().lower()
    if normalized in {"localhost", "127.0.0.1", "::1"}:
        return True
    try:
        return ipaddress.ip_address(normalized).is_loopback
    except ValueError:
        return False


def validate_bind_safety(host: str, token: str | None) -> None:
    """Reject non-loopback daemon binds when no HTTP bearer token is set."""
    if not token and not is_loopback_bind_host(host):
        raise ValueError(NON_LOOPBACK_TOKEN_REQUIRED_MESSAGE)


def make_dispatch_fn(gateway: Any) -> DispatchFn:
    """Build the single-flight ESP32 dispatcher used by the command queue."""

    async def dispatch(item: QueueItem) -> list[TextContent | ImageContent]:
        if not gateway.esp32.device_connected:
            return [
                TextContent(
                    type="text",
                    text=json.dumps(DISCONNECTED_DEVICE_PAYLOAD),
                )
            ]
        return await _dispatch_mcp_tool(item.tool_name, item.arguments, gateway)

    return dispatch


def build_app(
    queue: CommandQueue,
    *,
    gateway: Any,
    owner_id: str,
    host: str,
    port: int,
    token: str | None = None,
    dispatch_fn: DispatchFn | None = None,
    notify_config: NotifyConfig | None = None,
) -> _GuardedASGIApp:
    """Build the ASGI app for Streamable HTTP MCP plus health endpoints."""
    server = create_server(notify_config=notify_config)
    session_manager = StreamableHTTPSessionManager(
        app=server,
        json_response=True,
        stateless=False,
    )
    pending_items: dict[str, QueueItem] = {}
    _install_queue_tool_handler(
        server,
        queue=queue,
        gateway=gateway,
        pending_items=pending_items,
    )

    async def healthz(_request: Request) -> JSONResponse:
        return JSONResponse({"ok": True})

    async def status(_request: Request) -> JSONResponse:
        raw_status = gateway.esp32.get_status()
        status_payload = dict(raw_status) if isinstance(raw_status, dict) else {}
        if not isinstance(raw_status, dict):
            status_payload["status"] = raw_status
        status_payload.update(
            {
                "esp32_connected": bool(gateway.esp32.device_connected),
                "queue_depth": queue.depth,
                "queue_capacity": queue.capacity,
                "owner_id": owner_id,
                "connected_clients": _connected_client_count(session_manager),
            }
        )
        return JSONResponse(status_payload)

    async def agent_dashboard(_request: Request) -> JSONResponse:
        return JSONResponse(await gateway.get_agent_dashboard())

    @contextlib.asynccontextmanager
    async def lifespan(_app: Starlette):
        dispatcher_task: asyncio.Task[None] | None = None
        async with session_manager.run():
            if dispatch_fn is not None:
                dispatcher_task = asyncio.create_task(
                    queue.run_dispatcher(_skip_done_dispatch(dispatch_fn))
                )
            try:
                yield
            finally:
                if dispatcher_task is not None:
                    dispatcher_task.cancel()
                    with contextlib.suppress(asyncio.CancelledError):
                        await dispatcher_task
                _complete_pending_items_for_shutdown(pending_items)
                _drain_queued_items_for_shutdown(queue)

    routes = [
        Route(
            "/mcp",
            endpoint=_StreamableHTTPASGIApp(session_manager),
            methods=["GET", "POST", "DELETE"],
        ),
        Route("/healthz", endpoint=healthz, methods=["GET"]),
        Route("/status", endpoint=status, methods=["GET"]),
        Route("/agent-dashboard", endpoint=agent_dashboard, methods=["GET"]),
    ]
    app = Starlette(routes=routes, lifespan=lifespan)
    app.state.command_queue = queue
    app.state.session_manager = session_manager
    app.state.gateway = gateway
    return _GuardedASGIApp(
        app,
        token=token,
        allowed_hosts=_allowed_host_values(host, port),
    )


def _install_queue_tool_handler(
    server: Any,
    *,
    queue: CommandQueue,
    gateway: Any,
    pending_items: dict[str, QueueItem],
) -> None:
    async def handler(req: CallToolRequest) -> ServerResult | ErrorData:
        tool_name = req.params.name
        arguments = req.params.arguments or {}
        tool = await server._get_cached_tool_definition(tool_name)
        if tool is not None:
            try:
                jsonschema.validate(instance=arguments, schema=tool.inputSchema)
            except jsonschema.ValidationError as exc:
                return server._make_error_result(
                    f"Input validation error: {exc.message}"
                )

        if tool_name in BYPASS_TOOLS:
            content = await _dispatch_mcp_tool(tool_name, arguments, gateway)
            return _tool_result(content)

        context = server.request_context
        request = context.request
        client_session_id = None
        if isinstance(request, Request):
            client_session_id = request.headers.get(MCP_SESSION_ID_HEADER)

        response_future: asyncio.Future[Any] = asyncio.get_running_loop().create_future()
        item = QueueItem(
            correlation_id=str(uuid.uuid4()),
            client_session_id=client_session_id,
            client_request_id=context.request_id,
            tool_name=tool_name,
            arguments=arguments,
            response_future=response_future,
            enqueued_at=time.monotonic(),
        )
        try:
            queue.enqueue(item)
        except QueueFull as exc:
            return ErrorData(**build_queue_full_error(exc.queue_depth))

        pending_items[item.correlation_id] = item
        try:
            content_or_error = await response_future
        except asyncio.CancelledError:
            response_future.cancel()
            raise
        finally:
            if response_future.done():
                pending_items.pop(item.correlation_id, None)

        if isinstance(content_or_error, ErrorData):
            return content_or_error
        return _tool_result(content_or_error)

    server.request_handlers[CallToolRequest] = handler


def _skip_done_dispatch(dispatch_fn: DispatchFn) -> DispatchFn:
    async def dispatch(item: QueueItem) -> list[TextContent | ImageContent]:
        if item.response_future.done():
            return []
        return await dispatch_fn(item)

    return dispatch


def _complete_pending_items_for_shutdown(
    pending_items: dict[str, QueueItem],
) -> None:
    for item in list(pending_items.values()):
        _complete_item_with_shutdown_error(item)
    pending_items.clear()


def _drain_queued_items_for_shutdown(queue: CommandQueue) -> None:
    raw_queue = getattr(queue, "_queue")
    while True:
        try:
            item = raw_queue.get_nowait()
        except asyncio.QueueEmpty:
            return
        _complete_item_with_shutdown_error(item)
        raw_queue.task_done()


def _complete_item_with_shutdown_error(item: QueueItem) -> None:
    if not item.response_future.done():
        item.response_future.set_result(_server_shutdown_error())


def _server_shutdown_error() -> ErrorData:
    return ErrorData(
        code=SERVER_SHUTDOWN_ERROR_CODE,
        message=SERVER_SHUTDOWN_ERROR_MESSAGE,
        data={"reason": "server_shutdown"},
    )


def _tool_result(content: list[TextContent | ImageContent]) -> ServerResult:
    return ServerResult(
        CallToolResult(
            content=content,
            isError=False,
        )
    )


def _connected_client_count(session_manager: StreamableHTTPSessionManager) -> int:
    return len(getattr(session_manager, "_server_instances", {}))


def _allowed_host_values(host: str, port: int) -> set[str]:
    hosts = {host.strip().lower()}
    if is_loopback_bind_host(host) or is_wildcard_bind_host(host):
        hosts.update({"127.0.0.1", "localhost", "::1"})

    values: set[str] = set()
    for item in hosts:
        values.add(item)
        values.add(_host_with_port(item, port))
    values.update(_allowed_hosts_from_env(port))
    return values


def _allowed_hosts_from_env(port: int) -> set[str]:
    raw_hosts = os.getenv(MCP_HTTP_ALLOWED_HOSTS_ENV, "")
    values: set[str] = set()
    for raw_item in raw_hosts.split(","):
        item = raw_item.strip().lower()
        if not item:
            continue
        parsed = urlparse(item)
        if parsed.scheme in {"http", "https"} and parsed.netloc:
            item = parsed.netloc.lower()
        values.add(item)
        if ":" not in item or (item.startswith("[") and "]:" not in item):
            values.add(_host_with_port(item, port))
    return values


def _host_with_port(host: str, port: int) -> str:
    if ":" in host and not host.startswith("["):
        return f"[{host}]:{port}"
    return f"{host}:{port}"


def _is_allowed_host_header(value: str | None, allowed_hosts: set[str]) -> bool:
    if not value:
        return False
    return value.strip().lower() in allowed_hosts


def _is_allowed_origin(value: str | None, allowed_hosts: set[str]) -> bool:
    if not value:
        return True
    parsed = urlparse(value)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        return False
    return _is_allowed_host_header(parsed.netloc, allowed_hosts)


class _GuardedASGIApp:
    def __init__(
        self,
        app: Starlette,
        *,
        token: str | None,
        allowed_hosts: set[str],
    ) -> None:
        self._app = app
        self._token = token
        self._allowed_hosts = allowed_hosts
        self.state = app.state

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        if scope["type"] != "http":
            await self._app(scope, receive, send)
            return

        request = Request(scope, receive)
        if not _is_allowed_host_header(request.headers.get("host"), self._allowed_hosts):
            await PlainTextResponse(HOST_FAILURE_MESSAGE, status_code=403)(
                scope,
                receive,
                send,
            )
            return
        if not _is_allowed_origin(request.headers.get("origin"), self._allowed_hosts):
            await PlainTextResponse(ORIGIN_FAILURE_MESSAGE, status_code=403)(
                scope,
                receive,
                send,
            )
            return
        if self._token and scope.get("path") in {"/mcp", "/status", "/agent-dashboard"}:
            expected = f"Bearer {self._token}"
            if request.headers.get("authorization") != expected:
                await PlainTextResponse(
                    AUTH_FAILURE_MESSAGE,
                    status_code=401,
                    headers={"WWW-Authenticate": "Bearer"},
                )(scope, receive, send)
                return

        await self._app(scope, receive, send)

    async def router_startup(self) -> None:
        await self._app.router.startup()

    @property
    def router(self) -> Any:
        return self._app.router


class _StreamableHTTPASGIApp:
    def __init__(self, session_manager: StreamableHTTPSessionManager) -> None:
        self._session_manager = session_manager

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        await self._session_manager.handle_request(scope, receive, send)
