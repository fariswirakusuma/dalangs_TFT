# Architecture

## Layers

```text
main.c
  ├── board_display   SPI bus, ST7789/ILI9341, LVGL port
  ├── board_touch     XPT2046, calibration, LVGL input device
  ├── camera_tuner    esp32-camera init, frame task, sensor parameters
  ├── ui_host         native chrome, declarative widgets, canvas, timers
  ├── wasm_host       wasm3 environment, applet load, host imports
  ├── ui_model        pure widget descriptor registry (host-testable)
  └── system_monitor  SRAM/PSRAM/wasm load text for the status bar
```

Pure logic stays out of the ESP-IDF layer:

- `cam_params.c` clamps and validates sensor parameters.
- `ui_model.c` stores widget descriptors with a hard limit.

Both are compiled by `test/run.sh` on the host.

## Tasks

| Task | Core | Stack | Created by | Role |
| --- | --- | --- | --- | --- |
| `main` | 0 | 8 KB | ESP-IDF | Init and start |
| `lvgl` | 0 | 12 KB | `esp_lvgl_port` | Render, touch, timers, wasm calls |
| `camera` | 1 | 4 KB static | `xTaskCreateStaticPinnedToCore` | Grab and copy RGB565 frames |

The camera task is pinned to core 1 so capture does not compete with the LVGL
task on core 0. The LVGL task is pinned to core 0 deliberately; wasm executes on
the LVGL task because it touches UI state and the canvas.

LVGL timers:

| Period | Purpose |
| --- | --- |
| 33 ms | `wasm_host_tick()` for animated applets |
| 100 ms | Invalidate the camera image when a new frame arrived |
| 1000 ms | Refresh the native status bar |

## Data flow

### Camera preview

```text
OV5640 -> esp32-camera -> PSRAM fb (RGB565, 320x240)
       -> camera task memcpy -> persistent PSRAM frame
       -> LVGL image descriptor -> display flush
```

The persistent frame is 150 KB. The camera driver keeps two frame buffers in
PSRAM. The UI does not own the camera buffer, so returning the fb to the driver
is always safe.

### Camera parameter change

```text
LVGL slider/button event
  -> wasm_host_event(widget_id, value)
    -> app_event in wasm
      -> set_cam_param(param_id, value)
        -> cam_params_set (clamp)
          -> sensor_t setter
```

The wasm module never sees a pointer to a sensor or an LVGL object.

### Applet switch

```text
tab click
  -> ui_host_switch_applet(id)
    -> free the previous wasm runtime and module copy
    -> copy the embedded .wasm into PSRAM
    -> m3_NewRuntime + parse + load + link imports
    -> call app_init, which fills ui_model or draws the canvas
    -> render declarative widgets (or keep the canvas visible)
```

Load time is measured from the copy to the end of `app_init` and shown in the
status bar. It includes parse, instantiation, linking, and `app_init`.

## Memory budget

| Item | Location | Size |
| --- | --- | --- |
| Camera driver frame buffers (2x) | PSRAM | ~300 KB |
| Persistent preview frame | PSRAM | 150 KB |
| Canvas (RGB565) | PSRAM | 150 KB |
| Wasm module copy | PSRAM preferred | < 1 KB |
| Wasm linear memory | internal heap | 128 KB per runtime |
| LVGL draw buffers (2x) | internal DMA | ~50 KB |
| LVGL task stack | internal | 12 KB |

The wasm runtime is freed on every applet switch, so only one linear memory is
alive at a time. If internal SRAM becomes tight, reduce the wasm initial memory
in `scripts/build_wasm.sh` (it must stay page-aligned, so 64 KB is the practical
minimum).

## Failure handling

- Missing PSRAM: preview/canvas disabled, native UI still starts.
- Camera init failure: logged, preview stays black, tuner imports still work.
- Wasm load/trap failure: the native shell stays alive; the error is shown in
  the widget panel. A failing `app_tick` disables itself after the first error.
- Invalid widget count or label from wasm: the model rejects it; no LVGL object
  is created.
- Out-of-bounds wasm string: the host traps instead of reading host memory.

## Why wasm3

wasm3 is a small C interpreter with a straightforward raw-function ABI. That
makes it a good fit for tiny UI applets and for showing load-time and memory
overhead on the TFT. WAMR would be the choice if WASI or a larger feature set
were required.
