# Local Control WebSocket

StackChan exposes a LAN-only control WebSocket while the device is connected to
WiFi. This channel is separate from the Xiaozhi voice WebSocket, so robot
control can stay available even when the voice session is asleep.

## Firmware Endpoint

Connect directly to the device:

```text
ws://<device-ip>:8080/ws
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

The token is not sent in the WebSocket URL or headers. The socket opens only
far enough to complete an in-band HMAC challenge handshake.

Handshake:

1. Call `local_control/auth_begin`.
2. Compute `HMAC-SHA256(control_token, device_id:challenge_id:nonce:principal)`.
3. Call `local_control/auth_verify` with that proof.
4. Include the returned `session_token` on every later message.

Allowed principals:

- `dashboard`: local settings/token methods and MCP bridge
- `backend_control`: local settings/token methods and MCP bridge
- `mcp_bridge`: MCP bridge only
- `voice_bridge`: no local-control or MCP bridge access by default

Do not expose this endpoint to the public internet. For browser dashboards on
HTTPS, proxy through the backend and let the backend connect to the device over
plain LAN `ws://`.

## Authentication

Begin authentication:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "local_control/auth_begin",
  "params": {}
}
```

Example response:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "algorithm": "hmac-sha256-v1",
    "device_id": "stackchan-aabbccddeeff",
    "challenge_id": "0123456789abcdef0123456789abcdef",
    "nonce": "fedcba9876543210fedcba9876543210",
    "expires_in_ms": 30000,
    "principals": ["dashboard", "backend_control", "mcp_bridge", "voice_bridge"]
  }
}
```

Verify authentication:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "local_control/auth_verify",
  "params": {
    "challenge_id": "0123456789abcdef0123456789abcdef",
    "principal": "backend_control",
    "proof": "<hex hmac sha256>"
  }
}
```

Example response:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "session_token": "<session token>",
    "principal": "backend_control",
    "device_id": "stackchan-aabbccddeeff"
  }
}
```

## Message Format

After authentication, direct JSON-RPC messages must include `session_token` at
the top level:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "session_token": "<session token>",
  "method": "tools/list",
  "params": {}
}
```

The envelope form puts `session_token` beside the MCP payload:

```json
{
  "type": "mcp",
  "session_token": "<session token>",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/list",
    "params": {}
  }
}
```

Do not put `session_token` inside `params`; those params are forwarded to MCP
tool handlers.

## Useful Requests

Initialize MCP:

```json
{"jsonrpc":"2.0","id":3,"session_token":"<session token>","method":"initialize","params":{}}
```

List available tools:

```json
{"jsonrpc":"2.0","id":4,"session_token":"<session token>","method":"tools/list","params":{}}
```

Call a tool:

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "session_token": "<session token>",
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
  "id": 6,
  "session_token": "<session token>",
  "method": "settings/get",
  "params": {}
}
```

Validate and write SD settings. This is also local-control-only:

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "session_token": "<session token>",
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
  "id": 8,
  "session_token": "<session token>",
  "method": "local_control/set_token",
  "params": {
    "token": "stackchan-local-dev"
  }
}
```

## Proxying

Browsers on HTTPS should proxy through a backend. The backend should know the
device control token, complete the firmware handshake, and expose its own
authenticated browser-facing session. Do not expose the ESP32 WebSocket directly
outside the LAN.

The dashboard should treat these as separate states:

- WiFi/control presence: whether the local control WebSocket is reachable.
- Voice activity: whether the Xiaozhi voice WebSocket is currently connected.

## Current Limitations

- There is no TLS on the ESP32 endpoint by design; terminate TLS at the backend.
- A client that opens a WebSocket and never authenticates can occupy one of the
  small number of ESP HTTP server sockets until it disconnects or times out.
