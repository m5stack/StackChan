
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

and point reefwatch at the robot with `REEFWATCH_STACKCHAN_URL=http://<robot-ip>` and the same
`REEFWATCH_STACKCHAN_TOKEN`. The robot logs its address on boot (`listening on http://...`).
Protocol: `POST /frame` (JPEG ≤ 320×240, headers `X-Reefwatch-Token`, `X-Camera`, `X-Kind`, `X-Labels`)
and `GET /live` → `{"camera": "front" | null, "fps": 5}`. Code: `main/hal/hal_reefwatch.cpp`,
`main/apps/common/reefwatch_view/`.
