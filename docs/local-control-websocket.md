# Local Control WebSocket

StackChan exposes a LAN-only control WebSocket while the device is connected to
WiFi. This channel is separate from the voice WebSocket, so robot
control can stay available even when the voice session is asleep.

The method reference is generated from machine-readable metadata:

- [Generated local-control API reference](generated/local-control-websocket.md)
- [Generated local-control debug event reference](generated/local-control-events.md)
- [Generated voice WebSocket reference](generated/voice-websocket.md)
- Source metadata: `docs/api/local-control.methods.json`
- Generator: `tools/generate_api_docs.py`

Do not hand-edit method names, params, or result schemas in this file. The
pre-push hook checks generated docs against firmware source and blocks drift.

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

## Authentication

The token is not sent in the WebSocket URL or headers. The socket opens only
far enough to complete an in-band HMAC challenge handshake.

Handshake:

1. Call `local_control/auth_begin`.
2. Compute `HMAC-SHA256(control_token, device_id:challenge_id:nonce:principal)`.
3. Call `local_control/auth_verify` with that proof.
4. Include the returned `session_token` on every later message.

## Proxying

Browsers on HTTPS should proxy through a backend. The backend should know the
device control token, complete the firmware handshake, and expose its own
authenticated browser-facing session. Do not expose the ESP32 WebSocket directly
outside the LAN.

Clients should treat these as separate states:

- WiFi/control presence: whether the local control WebSocket is reachable.
- Voice activity: whether the voice WebSocket is currently connected.

## Current Limitations

- There is no TLS on the ESP32 endpoint by design; terminate TLS at the backend.
- A client that opens a WebSocket and never authenticates can occupy one of the
  small number of ESP HTTP server sockets until it disconnects or times out.
