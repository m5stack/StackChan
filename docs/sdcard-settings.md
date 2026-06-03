# StackChan SD Card Settings

The firmware reads optional settings from:

```text
/sdcard/stackchan/settings.json
```

The file overrides NVS settings in memory. It does not rewrite persistent NVS
values.

Example:

```json
{
  "xiaozhi": {
    "idleShutdownTimeSeconds": 600,
    "allowShutdownWhenCharging": false,
    "idleRandomMovementLevel": 2,
    "startAiAgentOnBoot": true
  }
}
```

Short NVS-style keys are also accepted:

```json
{
  "xiaozhi": {
    "idle_sec": 600,
    "ext_pwr": false,
    "idle_lv": 2,
    "boot_ai": true
  }
}
```

Runtime SD-card access in firmware code must use `GetHAL().withSdCard(...)`.
Do not call `fopen()`, `opendir()`, `stat()`, or similar directly under
`/sdcard` once the LCD is running.
