# Local Control WebSocket

StackChan exposes a LAN-only control WebSocket while the device is connected to
WiFi. This channel is separate from the Xiaozhi voice WebSocket, so robot
control can stay available even when the voice session is asleep.

## Firmware Endpoint

Connect directly to the device:

```text
ws://<device-ip>:8080/ws?token=<control-token>
```

The current development token is configured in firmware as:

```text
stackchan-local-dev
```

The token can be passed as either:

- `?token=stackchan-local-dev`
- `Authorization: stackchan-local-dev`
- `Authorization: Bearer stackchan-local-dev`

Do not expose this endpoint to the public internet. For browser dashboards on
HTTPS, proxy through the backend and let the backend connect to the device over
plain LAN `ws://`.

## Message Format

The WebSocket accepts JSON-RPC MCP messages directly:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/list",
  "params": {}
}
```

It also accepts an envelope form:

```json
{
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/list",
    "params": {}
  }
}
```

Responses are JSON-RPC MCP responses sent back on the same local WebSocket:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "tools": []
  }
}
```

## Useful Requests

Initialize MCP:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
```

List available tools:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
```

Call a tool:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "self.robot.set_head_angles",
    "arguments": {
      "pan": 0,
      "tilt": 0
    }
  }
}
```

Tool names and argument schemas come from `tools/list`.

## Server Proxy

The Go server adds authenticated proxy endpoints under the existing v2 API:

```text
GET /stackChan/v2/device/control/status?mac=<device-mac>
GET /stackChan/v2/device/control/ws?mac=<device-mac>&token=<user-token>
```

The proxy tracks the device IP from StackChan voice WebSocket connections and
then bridges browser traffic to:

```text
ws://<device-ip>:8080/ws?token=stackchan-local-dev
```

The dashboard should treat these as separate states:

- WiFi/control presence: whether the local control WebSocket is reachable.
- Voice activity: whether the Xiaozhi voice WebSocket is currently connected.

## Current Limitations

- The firmware control token is currently compile-time configured.
- The Go proxy only knows the device IP after a voice WebSocket connection has
  reported presence.
- There is no TLS on the ESP32 endpoint by design; terminate TLS at the backend.
