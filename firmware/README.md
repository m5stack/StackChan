
## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

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

Flash only the app partition for normal development:

```bash
make flash
```

Flash the bootloader, app, partition table, OTA data, and generated assets:

```bash
make fullflash
```

If flashing hangs at `Connecting...`, stop `ModemManager`; it can probe
Espressif USB CDC devices during reconnect.
