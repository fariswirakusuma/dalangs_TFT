# Wasm ABI

Every applet is a freestanding `wasm32` module. There is no libc, no WASI, and
no shared memory with the host. The host passes integers only; strings are
pointers into the module's own linear memory, validated by the host.

## Exports

| Export | Signature | Required | Purpose |
| --- | --- | --- | --- |
| `app_init` | `() -> ()` | Yes | Declare widgets or draw the initial canvas |
| `app_event` | `(i32 widget_id, i32 value) -> ()` | No | Slider change or button click |
| `app_tick` | `(i32 elapsed_ms) -> ()` | No | Animation step |
| `app_touch` | `(i32 x, i32 y, i32 pressed) -> ()` | No | Canvas pointer input |
| `memory` | linear memory | Yes | Required by wasm3 |

`app_init` is called once per load. `app_event` ids are assigned by the host in
widget creation order and are stable for the applet lifetime. A button event
passes `value = 1`; a slider event passes the new value.

## Imports

All imports use module name `env`.

| Import | Signature | Purpose |
| --- | --- | --- |
| `log` | `v(ii)` | UTF-8 pointer + byte length |
| `ui_slider` | `i(iiii)` | label ptr, min, max, default; returns widget id |
| `ui_button` | `i(i)` | label ptr; returns widget id |
| `set_cam_param` | `v(ii)` | parameter id, value |
| `reset_camera` | `v()` | restore all camera defaults |
| `canvas_clear` | `v(i)` | fill with RGB565 color |
| `canvas_rect` | `v(iiiii)` | x, y, width, height, RGB565 |
| `canvas_line` | `v(iiiii)` | x0, y0, x1, y1, RGB565 |
| `canvas_flush` | `v()` | invalidate the canvas |

Unknown imports fail the load. The host links every import above; an applet may
use a subset.

### Parameter IDs

| ID | Name | Range |
| --- | --- | --- |
| 0 | Brightness | -2..2 |
| 1 | Contrast | -2..2 |
| 2 | Saturation | -2..2 |
| 3 | Special effect | 0..6 |

The UI uses effects `0` (none), `2` (grayscale), `6` (sepia), `1` (negative).
The host clamps out-of-range values.

### Canvas

- Size is 320x240, RGB565, one pixel per 16-bit word.
- Clear to `0xF81F` (magenta) for transparency over the camera.
- Pixel coordinates are screen coordinates; the canvas is covered by the native
  status bar and tab bar.
- `canvas_flush` invalidates the LVGL canvas. Call it once after a batch of draw
  calls, not after every rectangle.
- A drawing applet should keep its pixels and not clear on every touch. An
  animated applet clears at the start of each tick.

## Applet source pattern

```c
#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

IMPORT(set_cam_param)
extern void set_cam_param(int param_id, int value);

IMPORT(ui_slider)
extern int ui_slider(const char *label, int min, int max, int default_value);

__attribute__((export_name("app_init"))) void app_init(void)
{
    ui_slider("Brightness", -2, 2, 0);
}

__attribute__((export_name("app_event"))) void app_event(int id, int value)
{
    set_cam_param(id, value);
}
```

## Build

```sh
make wasm
# or one file
scripts/build_wasm.sh main/wasm/draw_pad.c main/wasm/draw_pad.wasm
```

The script uses:

```text
clang --target=wasm32 -nostdlib -O2
wasm-ld --no-entry --export-memory
        --initial-memory=131072 --max-memory=262144
        --allow-undefined --gc-sections --strip-all
```

The 128 KB initial memory is the smallest page-aligned size that fits the
current applets. Increasing it costs internal heap per running runtime.

## Rules

- Bounded loops only. There is no preemption inside a wasm call.
- No host pointers, no WASI, no libc.
- Keep labels short; the host model truncates to 23 characters.
- Maximum 8 widgets per applet.
- Do not rely on a specific load time; it is measured and displayed, not fixed.
- Changing the ABI requires updating `wasm_host.c`, this document, and the
  `wasm-applet` skill in the same commit.
