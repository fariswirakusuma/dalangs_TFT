---
name: hardware-test
description: Bring-up and acceptance testing for dalangs_TFT hardware. Use when wiring the TFT, XPT2046, or camera, calibrating touch, checking memory, or validating a firmware change on the ESP32-S3 N16R8 board.
---

# Hardware Test

## Board contract

- ESP32-S3-WROOM N16R8: 16 MB flash, 8 MB octal PSRAM.
- Camera: OV2640/OV5640 DVP module on the ESP32-S3-CAM pinout.
- Display: 2.8 inch 240x320 SPI TFT with XPT2046 touch (14-pin module).
- Logic level: 3.3 V. Do not feed 5 V into the panel or touch controller.

Pin truth is `main/include/pins.h`; wiring prose is `docs/HARDWARE.md`. If they disagree, fix both.

## Bring-up order

1. Flash and open the monitor. Confirm the boot log reports the LCD driver and resolution.
2. Confirm the backlight turns on and the status bar is readable (not inverted, not shifted).
3. Touch each tab. If the axis is wrong, adjust `TOUCH_SWAP_XY`, `TOUCH_MIRROR_X`, `TOUCH_MIRROR_Y` in `pins.h`.
4. If the edges are off, calibrate `TOUCH_RAW_*` in `pins.h` with the four-corner method.
5. Confirm the camera initializes and the preview updates. A missing camera must not crash the UI.
6. Move each slider and confirm the image changes in real time.
7. Switch effect buttons and confirm Normal, Grayscale, Sepia, Negative.
8. Check the status bar: SRAM, PSRAM, and wasm load time move in a plausible range.

## Calibration

Touch raw values are 12-bit. Sample each corner, then set:

- `TOUCH_RAW_X_MIN`, `TOUCH_RAW_X_MAX`
- `TOUCH_RAW_Y_MIN`, `TOUCH_RAW_Y_MAX`

Keep the values inside the 50..4045 window used by the driver.

## Acceptance

- Cold boot reaches the first applet in under 5 seconds.
- No brownout or reboot while the camera and backlight are active.
- Applet switch does not leak: free SRAM after 20 switches is within a few KB of the first switch.
- TFT photo for the README shows the status bar values clearly.
