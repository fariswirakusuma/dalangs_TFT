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
| `camera` | 1 | 4 KB static | `start_camera_task()` in `main/main.c` | Grab, convert and downscale frames |

Every task this firmware creates is spawned in `main/main.c`, right above
`app_main()`; stack size, priority and core affinity are declared there, so the
boot audit (`CONFIG_DALANGS_FIRMWARE_AUDIT`) can be checked against one place.
Module code exposes a `*_task()` entry point and never spawns threads itself.

The camera task is pinned to core 1 so capture does not compete with the LVGL
task on core 0. The LVGL task is pinned to core 0 deliberately; wasm executes on
the LVGL task because it touches UI state and the canvas.

LVGL timers:

| Period | Purpose |
| --- | --- |
| 33 ms | `wasm_host_tick()` for animated applets |
| 100 ms | Invalidate the camera image when a new frame arrived |
| 1000 ms | Refresh the CPU badge and the BENCH values |

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
| Camera driver frame buffers (2x, VGA) | PSRAM | 1.2 MB |
| Persistent preview frame (QVGA) | PSRAM | 150 KB |
| Canvas (RGB565) | PSRAM | 150 KB |
| Wasm module copy | PSRAM preferred | < 1 KB |
| Wasm linear memory | internal heap | 64 KB per runtime |
| LVGL draw buffers (2x) | internal DMA | ~50 KB |
| LVGL task stack | internal | 12 KB |

The wasm runtime is freed on every applet switch, so only one linear memory is
alive at a time. If internal SRAM becomes tight, reduce the wasm initial memory
in `scripts/build_wasm.sh` (it must stay page-aligned, so 64 KB is the practical
minimum).

## Audits and asserts

Boot prints one line per invariant, and the ones that cannot be recovered from
assert instead of warning:

| Check | Where | Why |
| --- | --- | --- |
| Pin audit: no duplicate GPIO, none from the reserved list (USB, UART, PSRAM) | `main.c`, before the display claims its bus | two drivers on one pin silently break both |
| `LCD_PIN_CS != TOUCH_PIN_CS`, SCCB lines not on SPI lines | `_Static_assert` in `pins.h` | the LCD and touch share SPI2; the camera SCCB must stay off it |
| Canvas and camera frame are 1:1 | `_Static_assert` in `ui_host.c` | the canvas copies the frame straight in |
| VGA sensor is exactly 2x the output | `_Static_assert` in `camera_tuner.c` | the 2x2 downscale indexes by halves |
| Struct layouts (`cam_params_t`, `ui_widget_t`, `system_metrics_t`) | `_Static_assert` in the headers | padding creep shows up at compile time, not as a silent memory cost |
| Applet id in range | `assert` in `ui_host_switch_applet` | the tab tables are indexed by id |
| Preview and sensor handles non-NULL | `assert` at the camera task entry | the task only starts after a successful init |
| Heap integrity, free and largest block, per minute | audit + leak watch | a flat pair of numbers is the leak test |

Every struct is padding free. `cam_params_t` is four bytes, `ui_widget_t` is 28
(one byte per field plus the label) and `system_metrics_t` is 10: each value is
narrowed to a byte with a unit that keeps its range (KB/4, MB/16, hours, whole
degrees). `__attribute__((packed))` was not needed and would only have bought
bytes at the cost of byte-wise member access.

Shared state has a single writer: camera frames and the WB/AE state belong to
the camera task, widgets, parameters and the canvas belong to the LVGL task, and
nothing is written from both sides. The one deliberate exception is the preview
buffer: the camera task writes it while LVGL composites it, so a frame can tear
mid-flush. That is cosmetic and costs nothing to hold; double buffering it is the
fix if it ever becomes visible.

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
