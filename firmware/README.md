
## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Host-side tests

The motion coordinate helpers can be tested without ESP-IDF hardware:

```bash
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

### Flash

```bash
idf.py flash
```

## Reefwatch camera view (adampog fork)

Shows frames from [reefwatch](https://github.com/adampog/reefwatch) (ESP32-CAM + Coral object detection)
full screen on the robot, over any app and in AI agent mode.

- **Detections** show for 8 s with boxes and labels; LEDs go amber and, if configured, the head turns toward that camera.
- **Live view**: tap a detection to keep watching that camera, tap again to stop. In AI agent mode you can ask
  for it by voice (`self.cameras.show_live` / `self.cameras.stop_live` MCP tools). Switches off after 5 min.

Set a token (and optionally head angles) in `sdkconfig.defaults.local`:

```
CONFIG_STACKCHAN_REEFWATCH_TOKEN="change-me"
CONFIG_STACKCHAN_REEFWATCH_CAMERA_YAW="front=-45,garden=45"
```

and point reefwatch at the robot with `REEFWATCH_STACKCHAN_URL=http://stackchan.local` (or its IP) and the
same `REEFWATCH_STACKCHAN_TOKEN`. The robot logs its address on boot (`listening on http://...`).
Protocol: `POST /frame` (JPEG ≤ 320×240, headers `X-Reefwatch-Token`, `X-Camera`, `X-Kind`, `X-Labels`)
and `GET /live` → `{"camera": "front" | null, "fps": 5}`. Code: `main/hal/hal_reefwatch.cpp`,
`main/apps/common/reefwatch_view/`.

## LAN MCP server (adampog fork)

The robot's own MCP tools (`main/hal/hal_mcp.cpp`) are also served to agents on the LAN, such as
[ESP-Claw](https://github.com/adampog/ESP-Claw) running on another ESP32-S3: JSON-RPC over
`POST http://stackchan.local/mcp_server` (`initialize`, `ping`, `tools/list` with cursor paging,
`tools/call`), advertised over mDNS as `_mcp._tcp` with TXT `endpoint=mcp_server`, which is what
ESP-Claw's `mcp_client` discovers. Tools: head angles, LED color, reminders, live camera view, and
`self.robot.set_expression` / `self.robot.show_message` for agents that can't drive the voice
pipeline's emotions. The same tool objects are registered with xiaozhi, so the voice agent sees them too.

```bash
curl -s http://stackchan.local/mcp_server -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
curl -s http://stackchan.local/mcp_server -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"self.robot.set_expression","arguments":{"expression":"happy"}}}'
```

There is no authentication: anything on the LAN can move the head. Turn it off with
`CONFIG_STACKCHAN_LAN_MCP_ENABLE=n`; port, hostname and endpoint are in the "LAN Services" Kconfig menu.
Code: `main/hal/hal_lan_server.cpp`, `main/hal/hal_lan_mcp.cpp`.

## Voice gestures and LED moods (adampog fork)

Extra MCP tools in `main/hal/hal_mcp.cpp`, available to the voice agent and to LAN agents:

- `self.robot.nod` / `self.robot.shake_head` (`times` 1-5): yes/no gestures around the current head pose.
- `self.robot.dance` (`style`: `happy`, `robot`, `panic`, `look_around`): the built-in dance sequences.
- `self.robot.set_led_color` now holds its color until `self.robot.reset_led_color` returns to moods.

LED moods (`main/stackchan/addons/led_mood/led_mood.h`) drive all 12 LEDs from the agent's state:
amber pulse while connecting, soft green while listening, a pulse tinted by the face's emotion while
speaking (neutral blue, happy gold, angry red, sad deep blue, doubt purple), off when idle or asleep.
Priority is alert (reefwatch detection amber) > manual color > mood. Colors and pulse speeds are
constants at the top of that file.
