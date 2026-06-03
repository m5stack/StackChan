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
  },
  "localControl": {
    "token": "stackchan-local-dev"
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

Runtime settings methods are available only on the local control WebSocket:

- `settings/get`
- `settings/write_sd`
- `local_control/set_token`

`settings/write_sd` accepts a `settings_json` string, validates it, and only
writes the file if it is valid. It returns `reboot_required: true`; reboot
separately after changing settings that are loaded at startup, such as the local
control token.

Validation rules:

- `idleShutdownTimeSeconds` / `idle_sec`: integer `0..86400`
- `allowShutdownWhenCharging` / `ext_pwr`: boolean
- `idleRandomMovementLevel` / `idle_lv`: integer `0..3`
- `startAiAgentOnBoot` / `boot_ai`: boolean
- `localControl.token`: `8..96` characters, using only letters, numbers, `.`,
  `-`, `_`, and `~`

Runtime SD-card access in firmware code must use `GetHAL().withSdCard(...)`.
Do not call `fopen()`, `opendir()`, `stat()`, or similar directly under
`/sdcard` once the LCD is running.
