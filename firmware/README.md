
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

## Built-in Apps

- `AI.AGENT` – Xiaozhi voice AI assistant
- `AVATAR` – Remote avatar / WebSocket / video call mode
- `ESPNOW.REMOTE` – ESP-NOW wireless controller
- `APP.CENTER` – Online firmware/app launcher
- `EZDATA` – EzData cloud integration
- `DANCE` – BLE dance controller
- `MUSIC` – Offline microSD Music Player (`.wav` & `.mp3` from `/sd`, rhythm RGB LEDs & servo sway, NVS-persisted settings and last track)
- `SETUP` – Device settings, hardware tests, and app visibility toggles
