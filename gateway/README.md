# gateway

本目录随官方 StackChan Agent 看板整合，部署请优先阅读[中文配置说明](../docs/agent-tasks-setup.zh-CN.md)。源码包含本项目的 `/agent-dashboard` 与 Agent 适配器扩展，请从当前源码安装。下文保留上游通用网关说明；其中 WebSocket 设备控制、拍照和语音工具依赖上游替代固件，不是本项目官方固件的看板连接方式。


Python MCP gateway for the official [M5Stack StackChan](https://docs.m5stack.com/ja/StackChan) Agent Tasks dashboard. The upstream gateway also supports a separate xiaozhi-esp32 firmware dialect, which is not used by this repository's official firmware app.

```
┌─────────────┐  stdio MCP  ┌──────────────┐  WebSocket MCP  ┌──────────┐
│ MCP client  │ ──────────▶ │   gateway    │ ──────────────▶ │  ESP32   │
│ (Claude...) │ ◀────────── │  (this dir)  │ ◀────────────── │ StackChan│
└─────────────┘             │              │                 └──────────┘
                            │  /capture    │ ◀─ HTTP POST ──┘  (JPEG)
                            └──────────────┘
```

The gateway exposes a clean stdio MCP server to the LLM client (left) while
speaking the xiaozhi-esp32 WebSocket MCP dialect to the device (right). It
also runs a small HTTP server (`/capture`) so the ESP32 can upload photos.

The package name on PyPI, the installed CLI command, and the MCP server id
in your client config are all `stackchan-mcp`.

## Install (end users)

The gateway is published to PyPI as `stackchan-mcp`. For end users, install
it as an isolated CLI tool:

```bash
uv tool install stackchan-mcp
# or
pipx install stackchan-mcp
```

Then run:

```bash
stackchan-mcp
```

`stackchan-mcp` reads its configuration (`STACKCHAN_TOKEN`, `VISION_HOST`,
etc.) from environment variables or a `.env` file in the working directory.
See the [Setup](#setup) section below for the supported variables. For the
firmware side (WebSocket gateway URL, auth token, NVS configuration), see
[上游配置文档](https://github.com/kisaragi-mochi/stackchan-mcp#configuring-the-websocket-gateway-url-and-auth-token).

If you prefer a project-managed virtualenv, `pip install stackchan-mcp`
inside an active venv works as well, and `python -m stackchan_mcp` inside
that venv is equivalent to `stackchan-mcp`. Just avoid `pip install`
against the system Python (PEP 668).

## Setup

```bash
cd gateway
cp .env.example .env       # then edit .env (see below)
uv sync
```

Edit `.env`:
- `STACKCHAN_TOKEN`: Bearer token for ESP32 auth (must match firmware setting)
- `VISION_URL`: full public capture URL for remote access tunnels, such as
  `https://stackchan.example.ts.net:8443/capture`
- `VISION_TOKEN`: optional separate Bearer token for capture uploads; if empty,
  `STACKCHAN_TOKEN` is reused
- `VISION_HOST`: LAN IP of this machine, as seen from the ESP32
  (something like `192.168.x.y` on a typical home network — run `ifconfig`
  or `ip addr` to find it). Required for `take_photo` when `VISION_URL` is not
  set.
- `STACKCHAN_LISTEN_LANGUAGE`: default `listen()` language when the tool
  call omits `language` (falls back to `ja`). The MCP schema does not
  advertise a language default. `STACKCHAN_FASTER_WHISPER_*` select the
  local model; install the `[stt-faster-whisper]` extra.

## Run

```bash
uv run python -m stackchan_mcp
```

Default ports:
- WebSocket (ESP32 -> gateway): `0.0.0.0:8765`
- HTTP capture (ESP32 -> gateway): `0.0.0.0:8766`

## Daemon mode (Phase B)

For multi-client setups, run one shared Streamable HTTP daemon instead of
letting each MCP client spawn its own stdio gateway:

```bash
uv run stackchan-mcp serve --transport streamable-http
```

The daemon exposes MCP at `http://127.0.0.1:8767/mcp` by default, keeps the
existing ESP32 WebSocket and capture listeners, and serializes ESP32-bound
tool calls through a bounded command queue. See
[`../docs/178-daemon-setup.md`](../docs/178-daemon-setup.md) for environment
variables, bearer-token rules, `MCP_HTTP_ALLOWED_HOSTS`, bind safety, and
migration notes.

The zero-subcommand stdio mode remains supported and unchanged for existing
client configs.

By default, the gateway advertises the WebSocket endpoint as
`_stackchan-mcp._tcp.local.` via mDNS/DNS-SD so fresh firmware can discover it
on the local LAN. Run `stackchan-mcp --no-mdns` to disable this advertisement.

For non-LAN setups, see [`../docs/remote-access.md`](../docs/remote-access.md)
for the Tailscale Funnel flow.

When you restart the gateway during development, an already-connected ESP32
will notice the dropped WebSocket and retry while idle. The retry delay starts
at 5 seconds and backs off up to 60 seconds. After the gateway is listening
again, check `get_status` from the stdio MCP side to confirm the device is back.

## Configuration changes

The gateway reads `.env` once at process start. Because the gateway runs as a
**stdio MCP server** by default, editing `.env` while it is connected to an MCP
client does not take effect on the running process — and killing that stdio
gateway process directly will not auto-restart it; the MCP client owns the
lifecycle. In daemon mode, restart the daemon process after changing `.env`.

After editing `.env` (for example to update `STACKCHAN_TOKEN`, `VISION_URL`,
or `VISION_TOKEN`):

1. Reconnect the MCP client. In Claude Code this is `/mcp` to reconnect, or a
   full Claude Code restart.
2. Confirm `mcp__stackchan-mcp__get_status` returns `connected: true` with the
   expected `tools_count`.
3. If the ESP32 was already connected with a stale auth credential, hard-reset
   the device (`esptool.py --before default_reset --after hard_reset chip_id`,
   or DTR/RTS toggle via pyserial) so it reconnects with the fresh
   configuration.

`STACKCHAN_TOKEN` takes precedence over the legacy `BEARER_TOKEN`; setting
either is enough, but if you have both, keep them aligned.

## Tests

```bash
uv run pytest tests/ -v
```

## Register as MCP server

### Claude Code (`~/.claude.json`)

```json
{
  "mcpServers": {
    "stackchan-mcp": {
      "type": "stdio",
      "command": "uv",
      "args": [
        "run",
        "--directory",
        "/absolute/path/to/stackchan-mcp/gateway",
        "python",
        "-m",
        "stackchan_mcp"
      ],
      "env": {
        "STACKCHAN_TOKEN": "your-secret-token-here",
        "VISION_HOST": "your.host.lan.ip"
      }
    }
  }
}
```

### Claude Desktop (`claude_desktop_config.json`)

Same shape, under `mcpServers`.

## Tools exposed to MCP client

| Tool | Description |
|---|---|
| `get_status` | Gateway connection state (ESP32 connected? device info?) |
| `get_device_info` | ESP32 device status (battery, volume, WiFi, etc.) |
| `take_photo(question?)` | Trigger camera capture; returns the saved JPEG path plus the image itself as an inline MCP image block |
| `set_volume(volume)` | Speaker volume 0-100 |
| `set_brightness(brightness)` | Screen brightness 0-100 |
| `set_off_timeout(seconds)` | Set the idle screen-off timeout (integer 0..86400 seconds; default 300, 0 disables). Applied immediately and saved in NVS across reboots. Requires firmware 1.17.0+ |
| `get_off_timeout` | Read the effective persistent timeout, whether it is enabled, and whether the screen is off. Requires firmware 1.17.0+ |
| `set_external_power_auto_off(enabled)` | Persistently choose whether the idle auto-dim (60 s) and auto power-off (300 s) also run on external power. Default `false`: on USB/external power the device stays on; on battery it always dims and powers off when idle. Requires firmware-v1.18.0 or later. |
| `get_external_power_auto_off` | Read that setting, whether the battery is discharging, and whether the idle dim/power-off timer is running. |
| `set_charge_protection(enabled)` | Persistently enable/disable the opt-in battery-protection hysteresis. Set `false` before going out when you want the PMIC to finish a full charge. |
| `get_charge_protection` | Read the persistent/effective protection setting and current charger-enable state. |
| `set_charge_thresholds(on_below, off_above)` | Persist the charge-protection ON/OFF percentages (defaults: 30/70). |
| `move_head(yaw, pitch, speed?)` | Drive yaw + pitch servos |
| `get_head_angles` | Read current yaw + pitch servo angles |
| `get_touch_state` | Touch sensor state (press/release/stroke) |
| `set_avatar(face)` | Switch avatar expression (`idle` / `happy` / `thinking` / `sad` / `surprised` / `embarrassed`), or `off` to hide the avatar and disable blink so the underlying xiaozhi-esp32 screens (WiFi config UI, OTA, settings) are visible. A subsequent `set_avatar(<other face>)` brings it back and restores blink. |
| `set_blink(state)` | Blink animation on/off |
| `set_mouth(state)` | Mouth shape (`closed` / `half` / `open` / `e` / `u`), one-shot, held until next call |
| `set_mouth_sequence(steps)` | Queue and play a list of `{shape, duration_ms}` steps locally for TTS lip-sync. The firmware walks the queue without per-step network RTT. Calling `set_mouth`, `set_avatar`, or this tool again interrupts the in-flight sequence; autonomous blink is paused while a sequence is playing. |
| `check_vm_en` | Read PY32 VM EN GPIO state (servo power supply diagnostic) |
| `set_led(index, r, g, b)` | Set one of the 12 base RGB LEDs by index (`0..11`); channels `0..255`. Updates immediately. |
| `set_all_leds(r, g, b)` | Set all 12 base RGB LEDs to the same color. Updates immediately. |
| `set_leds(colors)` | Batch-set the first N LEDs from a `[[r,g,b], ...]` array (1..12 entries). Single I2C burst + one latch — use this for animations / multi-color patterns instead of N individual `set_led` calls. Trailing LEDs (beyond `len(colors)`) keep their previous color. Validation is atomic: a malformed entry rejects the whole call without mutating any LED. |
| `clear_leds` | Turn all 12 base RGB LEDs off. |
| `upgrade_firmware(firmware_path, timeout?)` | Flash a local `xiaozhi.bin` over Wi-Fi. Pre-flight checks (ESP32-S3 app image, project `xiaozhi`, board name, appended SHA-256, fits the OTA slot) run before the device is contacted; the image is then served once from `/firmware/{random id}` on the capture port (120 s TTL, no auth header, since the device's OTA client sends none), and the tool waits for reboot + reconnect and compares the running ELF SHA-256. Returns `ok`, `before`, `after`, `error`. |
| `beat_mode_start(motion_intensity?, sensitivity?, color?, duration_sec?)` | Start gateway-side beat mode: capture ambient audio through the existing `listen` wire path, estimate BPM locally, and drive free-running head sway plus base-ring LED flashes. `sensitivity` tunes the onset floor for quiet rooms or loud venues. `listen()` is mutually exclusive while active; `say()` may interrupt and beat mode re-arms listening afterwards. Requires the `[stt]` extra for Opus decoding. |
| `beat_mode_stop()` | Stop beat mode and retain the latest rolling audio buffer for clip export until the next beat-mode start or gateway restart. |
| `beat_mode_update(motion_intensity?, sensitivity?, color?, blink_rate?, motion_enabled?, led_enabled?)` | Update beat-mode motion/LED parameters without restarting capture. |
| `beat_meta_snapshot()` | Poll the latest beat metadata, active sensitivity/minimum onset floor, capture health, counters, and current motion/LED parameters. |
| `beat_clip_save(seconds?)` | Save the latest rolling beat-mode audio as a 16 kHz mono WAV temp file and return its path plus captured duration. |

The 12 base LEDs are 12× WS2812C wired to the PY32L020 IO expander
(expander pin 13, not an ESP32 GPIO), so all four LED tools share the
PY32 I2C bus with the servo-power and Si12T touch paths. If the PY32
init fails at boot, the LED tools degrade with `available=false`
instead of cascading errors.

The mapping from these names to ESP32-side `self.*` MCP tools is in
`stackchan_mcp/stdio_server.py`.

### Charge protection

`charge_protection` is opt-in (`false`) when it has never been saved and is
stored in ESP32 NVS across restarts. With protection on, firmware uses
NVS-backed thresholds (30%/70% by default): charging is enabled at or below
the lower value, disabled at or above the upper value, and unchanged between
them. With protection off, that
policy no longer stops charging; the AXP2101 charger is enabled and its own
full-charge termination remains in control. This is useful for topping up
before leaving home.

```text
set_charge_protection({"enabled": false})
get_charge_protection({})
set_charge_thresholds({"on_below": 35, "off_above": 75})
set_charge_protection({"enabled": true})
```

Changes take effect immediately without rebooting. Switching protection back
on immediately re-evaluates the current battery level. The firmware keeps the
public names; their physical NVS mapping is namespace `stackchan`, keys
`chg_protect`, `chg_on_below`, and `chg_off_above`, because ESP-IDF NVS names
allow at most 15 characters.

### Idle auto-dim / power-off

While no gateway is connected, the firmware dims the screen after 60 s idle
and powers the device off after 300 s idle. This runs **only on battery** by
default; on external power (USB) the device stays on.
`external_power_auto_off` (NVS namespace `stackchan`, key `ext_auto_off`,
`false` when unset) opts in to the same behaviour on external power as well.
It takes effect immediately and survives restarts.

```text
set_external_power_auto_off({"enabled": true})
get_external_power_auto_off({})
```

## Architecture

```
stackchan_mcp/
├── __main__.py         # entry: starts gateway + stdio server
├── gateway.py          # singleton orchestrator
├── stdio_server.py     # MCP client side (stdio MCP server)
├── esp32_client.py     # ESP32 side (WebSocket MCP client + auth)
├── capture_server.py   # HTTP /capture endpoint for photo uploads
├── server.py           # legacy local WS test server (unused in prod)
├── mcp_router.py       # legacy local stub router (unused in prod)
├── protocol.py         # JSON-RPC 2.0 message helpers
├── tools.py            # ESP32-side tool definitions (stub/test)
├── audio_stream.py     # placeholder for future Opus pipeline
└── handlers/
    ├── robot.py        # legacy stubs
    ├── camera.py       # legacy stubs
    └── audio.py        # legacy stubs
```

Captures land in `~/.stackchan/captures/` by default.

## Manual smoke test (Python)

```python
import asyncio, json, websockets

async def smoke():
    async with websockets.connect(
        "ws://localhost:8765",
        additional_headers={"Authorization": "Bearer your-secret-token-here"},
    ) as ws:
        await ws.send(json.dumps({
            "type": "hello", "version": 1, "audio_params": {},
        }))
        print(await ws.recv())

        await ws.send(json.dumps({"type": "mcp", "payload": {
            "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {},
        }}))
        print(await ws.recv())

        await ws.send(json.dumps({"type": "mcp", "payload": {
            "jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {},
        }}))
        print(await ws.recv())

asyncio.run(smoke())
```

## Phase roadmap

- **Phase 1** (done): stdio MCP shell, ESP32 WebSocket bridge, tool routing
- **Phase 2** (done): real servo / volume / brightness via ESP32
- **Phase 3** (done): camera capture (JPEG over HTTP)
- **Phase 4** (planned): Opus audio stream (STT/TTS pipeline)

## License

The gateway Python code is distributed under the **MIT License** (see
`LICENSE`). The Windows wheel (`*-win_amd64.whl`) additionally bundles
a native `opus.dll` built from upstream Opus source via vcpkg by the
publish workflow. That binary is distributed under the **BSD 3-clause
license + Xiph extension**; the full notice ships in every
distribution form (sdist, `py3-none-any` wheel, `win_amd64` wheel) as
`LICENSE-THIRD-PARTY`. Non-Windows wheels and the sdist do not contain
any binary subject to that license — they rely on a system `libopus`
provided by the OS package manager (e.g. `apt install libopus0`,
`brew install opus`). See `stackchan_mcp/_libs/SOURCES.md` (also
shipped in the wheel) for build provenance and the per-release
SHA256 logged by CI.

In this repository, the gateway and official firmware are separate
processes. The dashboard app reads a bounded, authenticated HTTP snapshot;
the gateway does not link firmware code into its Python package.
