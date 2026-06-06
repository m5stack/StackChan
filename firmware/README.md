
## Build

### Fetch Dependencies

```bash
git submodule update --init --recursive
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

### Wake Word Configuration

The default firmware configuration uses the ESP-SR WakeNet model selected in
`sdkconfig.defaults`. Changing the wake-word implementation or speech-recognition
model changes the generated firmware asset partition, so run `make fullflash`
after those config changes.

For a custom wake word, enable `CONFIG_USE_CUSTOM_WAKE_WORD` and select a
matching `CONFIG_SR_MN_*` MultiNet model. `CONFIG_CUSTOM_WAKE_WORD` is the model
command string, not always plain display text:

- Chinese models expect pinyin separated by spaces.
- `CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8` is registered through
  `esp_mn_commands_phoneme_add()`, so English custom commands must use the
  ESP-SR phoneme string for the selected phrase. Generate English phoneme
  strings with Espressif's `esp-sr/tool/multinet_g2p.py` helper.

`CONFIG_CUSTOM_WAKE_WORD_DISPLAY` is the text reported after detection.
`CONFIG_CUSTOM_WAKE_WORD_THRESHOLD` is a percentage; lower values are more
sensitive and may increase false positives.
