# Local Control WebSocket

StackChan exposes a LAN-only control WebSocket while the device is connected to
WiFi. This channel is separate from the Xiaozhi voice WebSocket, so robot
control can stay available even when the voice session is asleep.

## Firmware Endpoint

Connect directly to the device:

```text
ws://<device-ip>:8080/ws?token=<control-token>
```

The default development token is:

```text
stackchan-local-dev
```

Token precedence is:

```text
SD settings override > NVS setting > compiled default
```

Set an SD-card override in `/sdcard/stackchan/settings.json`:

```json
{
  "localControl": {
    "token": "your-token-here"
  }
}
```

Token changes loaded from SD settings require a reboot.

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

Read effective settings and any SD settings file. This is a local-control-only
method and is not forwarded to Xiaozhi MCP:

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "settings/get",
  "params": {}
}
```

Validate and write SD settings. This is also local-control-only:

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "settings/write_sd",
  "params": {
    "settings_json": "{\"xiaozhi\":{\"startAiAgentOnBoot\":true},\"localControl\":{\"token\":\"stackchan-local-dev\"}}"
  }
}
```

Validate and persist the local-control token to NVS:

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "local_control/set_token",
  "params": {
    "token": "stackchan-local-dev"
  }
}
```

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

Set the backend proxy token with:

```text
STACKCHAN_CONTROL_WS_TOKEN=<control-token>
```

If `localControl.token` is changed on the device, update this backend
environment variable to the same value before rebooting the device.

The dashboard should treat these as separate states:

- WiFi/control presence: whether the local control WebSocket is reachable.
- Voice activity: whether the Xiaozhi voice WebSocket is currently connected.

## Current Limitations

- The Go proxy only knows the device IP after a voice WebSocket connection has
  reported presence.
- There is no TLS on the ESP32 endpoint by design; terminate TLS at the backend.
