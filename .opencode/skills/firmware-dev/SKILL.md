---
name: firmware-dev
description: ESP-IDF firmware workflow for dalangs_TFT. Use when editing C sources, CMake, sdkconfig, pins, camera/display drivers, or the build/flash controller. Covers module boundaries, memory rules, build commands, and the hardware pin contract.
---

# Firmware Development

## Stack

- ESP-IDF v6.1, target `esp32s3`, C17, CMake + Ninja.
- LVGL 8.4 through `espressif/esp_lvgl_port`.
- Camera: `espressif/esp32-camera` (OV2640/OV5640, DVP).
- Display: built-in `esp_lcd` ST7789 or `espressif/esp_lcd_ili9341`.
- Touch: `atanisoft/esp_lcd_touch_xpt2046`.
- Wasm: vendored `wasm3` in `third_party/wasm3`, wrapped by `components/wasm3`.

## Layout rules

- Public headers live in `main/include`, implementations in `main/src`.
- One module per concern: board, camera, UI, wasm host, system monitor.
- `pins.h` is the only place with GPIO numbers. Never hard-code a pin elsewhere.
- Pure logic (parameter clamping, widget model) must stay independent of ESP-IDF so it can be host-tested in `test/`.
- All comments, logs, and documentation are English.

## Memory rules

- Camera frames, LVGL buffers, and wasm module copies go to PSRAM (`MALLOC_CAP_SPIRAM`).
- A failed PSRAM allocation must degrade gracefully: log, keep the UI alive, disable only the video path.
- Do not allocate in a render loop or an LVGL event callback.
- Use `xTaskCreateStaticPinnedToCore` for long-running tasks when the stack is known at compile time.
- Create every task from `main/src/tasks.c` (`app_tasks_start()`); a module exposes a `*_task()` entry point and never spawns threads itself, so stack, priority and core stay auditable in one place.
- Keep the LVGL task on core 0 and the camera capture task on core 1.

## Commands

```sh
make setup          # submodules + managed components
make build          # idf.py build (Ninja)
make flash          # PORT=/dev/ttyACM0 by default
make monitor
make test           # host unit tests, no board needed
make wasm           # rebuild the .wasm applets
make clean          # idf.py fullclean
```

## Definition of done

- `make test` and `make build` pass.
- No new warnings in project sources.
- New pins or wiring changes are reflected in `docs/HARDWARE.md`.
- A new wasm import is reflected in `docs/WASM_ABI.md` and the applet skill.
