# Unixtreme StackChan Firmware Fork

This is my personal fork of the [M5Stack StackChan](https://github.com/m5stack/StackChan)
project.

This fork exists because I want a different product shape: more local-first,
more explicit about network behavior, and easier for me to inspect and extend.

## Project Goals

After getting my StackChan and looking at its network behavior, I was not
comfortable with firmware paths that could contact remote services without a
clear local decision from the user. A major goal of this fork is to make network
behavior explicit and opt-in wherever practical.

The longer-term goal is to create a lean base for expanding the AI Agent while
still hiding unnecessary complexity from normal users. The device should remain
pleasant to use, but the firmware contract should be explicit enough that a
custom server, dashboard, or local automation layer can control it without
reverse engineering.

## Why Use This Fork?

This fork focuses on StackChan as a more local-first AI Agent device. It removes
the direct xiaozhi-esp32 firmware dependency, reduces upstream remote-service
coupling, and documents the local contracts this fork currently relies on.

- Network behavior is intended to be explicit and opt-in. Known upstream remote
  service paths have been disabled or guarded where found.
- Authenticated local control WebSocket for settings, robot actions, device
  status, and firmware-owned tools.
- HMAC challenge/response local-control authentication uses session tokens and
  named principals for dashboards, admin tools, MCP bridges, and voice bridges.
- Voice WebSocket transport can be configured locally, including endpoint and
  token, instead of relying on upstream-seeded voice service settings.
- Voice WebSocket traffic is being moved toward explicit authentication and
  session-bound messages.
- JSON-RPC and MCP handling has been tightened, with generated machine-readable
  and human-readable API documentation.
- Auto-generated API docs that are checked against firmware source strings by
  local tooling, so documented remote methods do not silently drift from the
  implementation.
- Host-side API smoke tests can exercise authentication, settings, voice
  control, expected rejects, and documented schemas against a live device.
- SD-card settings overrides for practical field testing and recovery.
- Runtime SD-card access is guarded through firmware helpers so the CoreS3 LCD
  and SD card do not fight over the shared GPIO35/SPI path.
- Firmware identity metadata, including fork identity, exposed through runtime
  APIs and the on-device Firmware settings UI.
- OTA and activation paths reject known upstream service URLs discovered during
  this work, reducing the chance that a stock or previously-seeded device drifts
  back toward the old remote service flow.
- Reduced firmware baggage by removing unused board, display, GIF, LED, codec,
  and vendor paths that are not relevant to the target StackChan/CoreS3 build.
- Asset packaging through `assets.tar.gz`, keeping generated/binary asset
  noise out of the normal source tree while preserving deterministic builds.
- Simple firmware `make build`, `make flash`, and `make fullflash` targets use a
  repo-relative ESP-IDF path by default instead of depending on machine-local
  absolute paths.
- CI builds produce downloadable flash bundles with binaries, `flash_args`,
  checksums, and flashing instructions.
- xiaozhi-esp32 is no longer cloned and patched during firmware builds; the
  remaining firmware runtime now lives in this tree where it can be reviewed
  directly.
- Future server/dashboard work is expected to build on the local protocol first,
  with higher-level bridges routing only authorized capabilities.

The current fork-specific documentation is here:

- [Local control WebSocket API](docs/local-control-websocket.md)
- [SD-card settings](docs/sdcard-settings.md)
- Generated API docs under `docs/generated/`
- API metadata under `docs/api/`

Pipeline-generated firmware bundles can be used to flash compatible
StackChan/CoreS3 hardware. They include the firmware binaries, `flash_args`,
checksums, and short flashing instructions. The default upstream xiaozhi server
does not implement this fork's local-control protocol, so these fork-specific
remote-control features require a compatible custom server/dashboard.

## License

Code in this fork is distributed under the MIT License except where otherwise
noted: [LICENSE](./LICENSE).

Bundled third-party assets remain under their respective licenses. Physical
license texts and legal notices are included under
[third_party_licenses/](third_party_licenses/).

## Attributions

### Code

- [M5Stack StackChan](https://github.com/m5stack/StackChan) is still the source
  of much of the firmware, app shell, UI, hardware integration, and device
  behavior. StackChan is distributed under the MIT License.
- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) informed the original AI
  Agent/runtime direction. This fork is progressively replacing that runtime
  surface with firmware-owned code, but attribution is retained for the original
  implementation lineage. xiaozhi-esp32 is distributed under the MIT License.

### Sound Files

Most bundled sound files are inherited from the upstream StackChan firmware
asset set. I did not find separate per-file licensing notices in the source
tree, so they are currently treated as covered by the upstream StackChan MIT
license unless proven otherwise.

Current bundled sound groups:

- Locale voice prompts under `main_assets/locales/*/*.ogg`
- Common UI sounds: `exclamation.ogg`, `low_battery.ogg`, `popup.ogg`,
  `success.ogg`, `vibration.ogg`
- Camera/app sounds: `camera_shutter.ogg`, `new_notification.ogg`

### Images

StackChan/M5 UI assets, currently treated as covered by the upstream StackChan
MIT license:

- `app_center_bg.png`
- `icon_ai_agent.bin`
- `icon_app_center.bin`
- `icon_bat_lightning.bin`
- `icon_bell.bin`
- `icon_controller.bin`
- `icon_dance.bin`
- `icon_ezdata.bin`
- `icon_home.bin`
- `icon_indicator_left.bin`
- `icon_indicator_right.bin`
- `icon_sentinel.bin`
- `icon_setup.bin`
- `icon_wifi_high.bin`
- `icon_wifi_low.bin`
- `icon_wifi_medium.bin`
- `icon_wifi_slash.bin`
- `setup_stackchan_front_view.bin`

Twemoji graphics:

- `twemoji_32/*.png`
- `twemoji_64/*.png`

Twemoji code is MIT-licensed, but the Twemoji graphics shipped here are licensed
under Creative Commons Attribution 4.0 International.

### Fonts

- Montserrat: `MontserratSemiBold26.c` is generated from Montserrat SemiBold.
  Montserrat is distributed under the SIL Open Font License.
- Font Awesome: `font_awesome_20_4.c` is generated from Font Awesome Free. Font
  Awesome Free uses SIL OFL for fonts, CC BY 4.0 for icons, and MIT for code.
- Alibaba PuHuiTi / 阿里巴巴普惠体: `font_puhui_basic_20_4.c` and
  `font_puhui_common_20_4.bin` appear to be generated from Alibaba PuHuiTi
  assets. Alibaba PuHuiTi is free for personal and commercial use under
  Alibaba's font legal terms, but it is not an open-source font license.

### Firmware Model Assets

The generated firmware asset partition also includes the ESP-SR wake-word model
from Espressif's managed ESP-SR component. This is not stored in
`main/assets/assets.tar.gz`, but it is included in generated firmware artifacts.
Its licensing follows Espressif ESP-SR/component terms.
