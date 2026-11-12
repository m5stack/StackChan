# Remote Access with Tailscale Funnel

This guide explains how to make a `stackchan-mcp` gateway reachable when the
StackChan device and gateway host are on different networks.

The recommended first path is Tailscale Funnel:

- no firmware feature work is required
- the ESP32 connects to a public `wss://...ts.net` gateway URL
- the gateway still verifies the ESP32 bearer token with `STACKCHAN_TOKEN`
- photo uploads can use a second Funnel URL through `VISION_URL` and are
  protected with `VISION_TOKEN` or, by default, `STACKCHAN_TOKEN`

Tailscale Funnel publishes local services on a device in your tailnet to the
public internet over HTTPS. It requires Tailscale CLI support, MagicDNS, HTTPS
certificates, and a Funnel node attribute in the tailnet policy. Funnel can
listen only on `443`, `8443`, or `10000`, so this guide uses `443` for the
WebSocket gateway and `8443` for the photo capture endpoint.

Reference:

- [Tailscale Funnel](https://tailscale.com/docs/features/tailscale-funnel)
- [tailscale funnel command](https://tailscale.com/docs/reference/tailscale-cli/funnel)
- [Tailscale Funnel examples](https://tailscale.com/docs/reference/examples/funnel)

## Network Shape

```
ESP32 StackChan
    │
    │ wss://<node>.<tailnet>.ts.net/
    ▼
Tailscale Funnel :443
    │
    │ forwards to local TCP
    ▼
stackchan-mcp gateway WebSocket :8765

ESP32 camera upload
    │
    │ https://<node>.<tailnet>.ts.net:8443/capture
    ▼
Tailscale Funnel :8443
    │
    │ forwards to local HTTP
    ▼
stackchan-mcp capture server :8766
```

## Security Notes

Funnel makes the selected service public. Keep `STACKCHAN_TOKEN` set and use a
strong token for the WebSocket gateway.

The `/capture` endpoint is a separate HTTP upload endpoint. Keep
`STACKCHAN_TOKEN` set so the gateway can pass a bearer token to the ESP32 for
photo uploads, or set `VISION_TOKEN` if you want a separate capture token. If
you expose capture with Funnel, still treat the URL as public while the Funnel
listener is enabled, turn it off when it is not needed, and do not expose
captured photos or other user data in git.

Do not publish personal tokens, LAN IP addresses, WiFi credentials, `.env`
files, captures, or local firmware override files.

If you only need access from devices that already run Tailscale, use Tailscale
Serve or a normal tailnet address instead of Funnel. The ESP32 firmware does
not currently run a Tailscale client, so the StackChan device itself needs a
public Funnel URL unless a separate network layer routes it into the tailnet.

## Gateway Configuration

In `gateway/.env`, keep the gateway listening locally and set both the ESP32
auth token and the public capture URL:

```bash
STACKCHAN_TOKEN=<strong-shared-token>

HOST=0.0.0.0
WS_PORT=8765
CAPTURE_PORT=8766

VISION_URL=https://<node>.<tailnet>.ts.net:8443/capture
# Optional: leave empty to reuse STACKCHAN_TOKEN.
VISION_TOKEN=
```

`VISION_URL` is the full URL sent to the ESP32 for `take_photo` uploads. Use it
for remote tunnel setups. `VISION_TOKEN` is sent to the ESP32 as the capture
upload bearer token; if it is empty, the gateway reuses `STACKCHAN_TOKEN`.
`VISION_HOST` remains useful for LAN-only setups where the capture URL is
`http://<lan-ip>:8766/capture`.

Start the gateway:

```bash
cd gateway
uv run python -m stackchan_mcp
```

## Funnel Setup

On the same host as the gateway, enable two Funnel listeners.

Use port `443` for the WebSocket gateway:

```bash
tailscale funnel --bg --https=443 http://localhost:8765
```

Use port `8443` for the HTTP capture server:

```bash
tailscale funnel --bg --https=8443 http://localhost:8766
```

Check the published URLs:

```bash
tailscale funnel status
```

The expected public endpoints are:

- WebSocket: `wss://<node>.<tailnet>.ts.net/`
- Capture: `https://<node>.<tailnet>.ts.net:8443/capture`

To stop the listeners:

```bash
tailscale funnel --https=443 off
tailscale funnel --https=8443 http://localhost:8766 off
```

## Device Configuration

Configure the firmware's WebSocket URL and bearer token:

```text
websocket.url = wss://<node>.<tailnet>.ts.net/
websocket.token = <strong-shared-token>
```

For developer builds, these can be provided through the existing Kconfig
fallbacks:

```text
CONFIG_DEFAULT_WEBSOCKET_URL="wss://<node>.<tailnet>.ts.net/"
CONFIG_DEFAULT_WEBSOCKET_TOKEN="<strong-shared-token>"
```

To prefer a LAN gateway when the StackChan and gateway host are on the same
network, while still keeping a remote Funnel path for travel, configure the
local URL as primary and the Funnel URL as fallback:

```text
CONFIG_DEFAULT_WEBSOCKET_URL="ws://<gateway-host>:8765/"
CONFIG_DEFAULT_WEBSOCKET_FALLBACK_URL="wss://<node>.<tailnet>.ts.net/"
CONFIG_DEFAULT_WEBSOCKET_TOKEN="<strong-shared-token>"
```

At runtime the firmware tries the primary URL first. If the TCP/WebSocket
connection fails, or if the stackchan-mcp server hello does not complete, it
tries the fallback URL next. Leave the fallback empty for local-only or
Tailscale-only setups.

For existing devices with stale NVS values, use the documented
`CONFIG_FORCE_DEFAULT_WEBSOCKET_URL=y` flow from the top-level README.

## Validation

After the gateway, Funnel, and device settings are in place:

1. Start the gateway.
2. Start both Funnel listeners.
3. Reboot or reset the StackChan device.
4. Press the StackChan main button to start a chat session. The firmware opens
   the WebSocket connection when a session starts; an idle device may not appear
   in `get_status` immediately after boot.
5. Call `get_status` from the MCP client and confirm `connected=true`.
6. Call `get_device_info`.
7. Call `take_photo` and confirm the capture reaches the gateway.

If the device and gateway are on the same physical LAN during validation, this
still verifies that the firmware uses the public Funnel `wss://` URL, bearer
auth, MCP initialization, and the authenticated capture callback. It does not
prove behavior across every remote network path. For a stronger end-to-end
remote proof, place the StackChan device on a different network, such as a phone
hotspot, while keeping the gateway host behind Funnel.

If `get_status` works but `take_photo` fails, the WebSocket path is working but
the capture callback URL is wrong. Re-check `VISION_URL`, the `8443` Funnel
listener, and whether the capture URL ends with `/capture`.

If `take_photo` is not needed for a remote session, leave the `8443` Funnel
listener off and use only the WebSocket listener.

## Limitations

- Funnel bandwidth limits are managed by Tailscale and are not configurable.
- Latency depends on the device WiFi, the gateway network, and Funnel relay path.
- The future Opus audio stream may need lower and more stable latency than
  simple control and photo capture.
- A firmware-native WireGuard or Tailscale-style client would be a separate
  follow-up feature, not part of this guide.
