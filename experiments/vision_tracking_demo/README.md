# StackChan Vision Tracking Demo

This is a standalone ESP-IDF experiment for evaluating visual gaze tracking on
the StackChan / CoreS3 hardware before integrating it into the production
firmware.

The demo initializes only the hardware needed for the experiment:

- GC0308 DVP camera at 320x240 RGB565
- ESP-DL `human_face_detect`
- ILI9341 display debug overlay
- yaw / pitch serial servos

It intentionally does not start the full AI Agent stack, Wi-Fi, MQTT, audio AFE,
LVGL app flow, or the normal idle animation system. That makes it easier to test
camera capture, face detection, smoothing, and servo response in isolation.

## Build and Flash

From this directory:

```sh
. /Users/xurui/esp/esp-idf-v5.5.4/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem21301 flash monitor
```

Adjust the serial port if the device enumerates differently.

## What It Tests

- Camera bring-up through `esp_video`
- RGB565 frame handling for `human_face_detect`
- Face center smoothing and short target hold
- Servo rate limiting and deadband tuning
- A screen dot that marks the current tracked face position for debugging

## Experiment Result

The standalone demo proved that the current camera and ESP-DL face detection can
form a basic closed-loop face tracking demo after smoothing and servo tuning.
However, integrating the same direction into the full AI Agent firmware exposed
resource limits on the current hardware.

During full firmware experiments, the AI Agent path already had audio AFE,
wake-word processing, Wi-Fi/MQTT, UI, camera, and servo tasks active. Adding
ESP-DL based visual tracking caused severe internal SRAM pressure and real-time
instability. Observed symptoms included very low remaining internal SRAM, AFE
feed ringbuffer warnings, MQTT/Wi-Fi instability, sluggish tracking, jitter, and
reboots in some iterations.

The current conclusion is that this demo is useful as a reproducible experiment,
but the production firmware should not merge the visual tracking path until the
hardware/resource budget issue is solved. Possible next directions are a more
constrained asynchronous vision pipeline, a lower-cost detector, a larger app and
RAM budget, or offloading vision to stronger hardware.
