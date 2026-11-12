# Issue #178 Daemon Setup

The gateway now has two supported MCP server modes:

- **stdio mode**: `stackchan-mcp` with no subcommand. This is the original
  single-client mode used by existing MCP client configs.
- **daemon mode**: `stackchan-mcp serve --transport streamable-http`. This
  starts one long-lived gateway process, keeps one ESP32 WebSocket connection,
  and accepts multiple MCP clients over Streamable HTTP.

Use daemon mode when more than one MCP client or automation needs to share the
same StackChan device. Keep stdio mode for simple local setups, compatibility
testing, and existing clients that do not yet support Streamable HTTP.

## Start the daemon

```bash
cd gateway
uv run stackchan-mcp serve --transport streamable-http
```

The daemon starts the same ESP32 WebSocket listener and capture server as stdio
mode, then exposes the MCP endpoint at:

```text
http://127.0.0.1:8767/mcp
```

The zero-subcommand stdio entry point remains supported and unchanged:

```bash
stackchan-mcp
```

`stackchan-mcp serve --transport stdio` is equivalent to the zero-subcommand
stdio path.

## Environment variables

| Variable | Default | Purpose |
|---|---:|---|
| `STACKCHAN_TOKEN` | unset | Bearer token shared with the ESP32 firmware and, when set, required by HTTP MCP clients. |
| `BEARER_TOKEN` | unset | Legacy alias used only when `STACKCHAN_TOKEN` is unset. |
| `MCP_HTTP_HOST` | `127.0.0.1` | Bind host for the Streamable HTTP MCP endpoint. |
| `MCP_HTTP_PORT` | `8767` | Bind port for the Streamable HTTP MCP endpoint. |
| `MCP_HTTP_ALLOWED_HOSTS` | unset | Comma-separated Host / Origin allowlist for non-loopback clients, for example `192.168.1.10,stackchan.local:8767`. |
| `STACKCHAN_COMMAND_QUEUE_SIZE` | `32` | Maximum queued ESP32-bound tool calls before the daemon returns backpressure. |

The existing gateway variables (`HOST`, `WS_PORT`, `CAPTURE_PORT`,
`VISION_HOST`, `VISION_URL`, `VISION_TOKEN`, and audio hook settings) keep the
same meaning in daemon mode.

## Bind safety

Loopback binds (`127.0.0.1`, `::1`, or `localhost`) may run without an HTTP
bearer token for local development.

Non-loopback binds, such as `0.0.0.0` or a LAN IP address, require
`STACKCHAN_TOKEN` or `BEARER_TOKEN`. Startup is refused otherwise:

```text
stackchan-mcp: refusing non-loopback MCP_HTTP_HOST without STACKCHAN_TOKEN or BEARER_TOKEN
```

When a token is configured, every request to `/mcp` and `/status` must include:

```text
Authorization: Bearer <token>
```

`/healthz` is intentionally unauthenticated so local process monitors can check
whether the daemon is alive. Host and Origin headers are still validated on all
daemon endpoints.

For wildcard binds such as `MCP_HTTP_HOST=0.0.0.0`, loopback Host values
(`127.0.0.1`, `localhost`, `::1`) are accepted by default. LAN clients usually
send the machine's LAN IP or DNS name in the `Host` header, so add those values
to `MCP_HTTP_ALLOWED_HOSTS`:

```bash
STACKCHAN_TOKEN=your-secret-token-here \
MCP_HTTP_HOST=0.0.0.0 \
MCP_HTTP_ALLOWED_HOSTS=192.168.1.10,stackchan.local:8767 \
stackchan-mcp serve --transport streamable-http
```

## MCP client configuration

Use a Streamable HTTP MCP client pointed at `/mcp`. Example
`mcp.config.json`:

```json
{
  "mcpServers": {
    "stackchan-mcp": {
      "type": "streamable-http",
      "url": "http://127.0.0.1:8767/mcp",
      "headers": {
        "Authorization": "Bearer your-secret-token-here"
      }
    }
  }
}
```

If no `STACKCHAN_TOKEN` or `BEARER_TOKEN` is configured and the daemon is bound
to loopback, omit the `headers` block.

## Operational endpoints

`GET /healthz` returns daemon-local health:

```json
{
  "ok": true,
  "esp32_connected": true,
  "queue_depth": 0,
  "queue_capacity": 32,
  "owner_id": "stackchan-mcp-1234abcd"
}
```

`GET /status` returns the same gateway status as the `get_status` MCP tool,
plus `queue_depth`, `queue_capacity`, and `connected_clients`.

## Migration notes

Existing stdio configs do not need to change. The following remains valid:

```json
{
  "mcpServers": {
    "stackchan-mcp": {
      "type": "stdio",
      "command": "stackchan-mcp"
    }
  }
}
```

To migrate, start the daemon as an independent process first, then point each
Streamable HTTP-capable MCP client at `http://127.0.0.1:8767/mcp`. Once a
client uses the daemon, it should not also spawn `stackchan-mcp` in stdio mode
for the same device, because the daemon is the process that owns the ESP32
WebSocket.

The daemon keeps a single bounded FIFO command queue for ESP32-bound tool
calls. `get_status` bypasses the queue because it reads gateway-local state.
When the queue is full, the daemon returns a JSON-RPC error with code `-32000`
and message `stackchan command queue is full`; it does not buffer commands
without a limit.
