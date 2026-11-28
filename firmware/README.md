
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

### Optional AI.AGENT playback trial (StackChan CoreS3)

If streamed replies cut out mid-word but music and local microphone playback are
smooth, the `STACKCHAN_AI_AUDIO_TRIAL` build option can help compare the two
playback configurations. It is disabled by default. In an ESP-IDF v6.1 shell,
run `idf.py menuconfig`, press `/`, search for `STACKCHAN_AI_AUDIO_TRIAL`,
enable it, save and run `idf.py build`. For a fresh build you can also set
`CONFIG_STACKCHAN_AI_AUDIO_TRIAL=y` in `sdkconfig.defaults.local`.

The trial prefers an offered WebSocket and lets its receive callback wait for
room in the decode queue. It keeps the existing MQTT receive behavior when
only MQTT is available. Both CoreS3 I2S rings use eight descriptors instead
of six (80 ms instead of 60 ms at 24 kHz, 240 frames per descriptor). It uses
more internal DMA memory. For comparisons, record time to first audio, missing
syllables during a reply, wake-up, interruption and reconnect behavior.
Public builds have limited StackChan World integration as described in
[issue #134](https://github.com/m5stack/StackChan/issues/134).

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
