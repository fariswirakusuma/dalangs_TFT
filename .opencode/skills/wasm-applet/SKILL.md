---
name: wasm-applet
description: Build and debug WebAssembly UI applets for dalangs_TFT. Use when adding an applet, changing the host ABI, compiling .wasm with clang, or debugging wasm3 load/trap errors.
---

# Wasm Applet

## Toolchain

Applets are freestanding `wasm32` modules. No libc, no WASI.

```sh
make wasm
# or
scripts/build_wasm.sh main/wasm/basic_sliders.c main/wasm/basic_sliders.wasm
```

The build script uses `clang --target=wasm32 -nostdlib`, `wasm-ld`, `--no-entry`, and explicit exports. System clang needs a `wasm-ld`; the script falls back to the one bundled with Espressif clang.

## Module contract

Required export: `app_init()`.
Optional exports: `app_event(i32 widget_id, i32 value)`, `app_tick(i32 elapsed_ms)`, `app_touch(i32 x, i32 y, i32 pressed)`.

Imports (module `env`):

| Import | Signature | Purpose |
| --- | --- | --- |
| `log` | `v(ii)` | pointer + byte length, host logs UTF-8 |
| `ui_slider` | `i(iiii)` | label ptr, min, max, default; returns widget id |
| `ui_button` | `i(i)` | label ptr; returns widget id |
| `set_cam_param` | `v(ii)` | param id, value |
| `reset_camera` | `v()` | restore camera defaults |
| `canvas_clear` | `v(i)` | fill with RGB565 color; `0xF81F` is transparent |
| `canvas_rect` | `v(iiiii)` | x, y, w, h, RGB565 |
| `canvas_line` | `v(iiiii)` | x0, y0, x1, y1, RGB565 |
| `canvas_flush` | `v()` | invalidate the canvas |

Widget ids are assigned by the host in creation order and are stable for the applet lifetime.

## Rules

- Keep every loop bounded. There is no preemption inside a wasm call.
- No global constructors and no libc calls.
- Read no host memory; use the exported imports only.
- Clear the canvas each tick only when animating. Drawing applets keep their pixels.
- Stay below 64 KB of linear memory unless the host ABI docs are updated.
- Run `make wasm`, then `make build`; the .wasm files are embedded into the firmware.

## Debugging

- A load error is printed with the wasm3 message and the applet name.
- A trap in `app_init` aborts the applet switch; the native UI stays alive.
- Check exported names with `wasm-objdump -x file.wasm` if available, or `llvm-objdump`.
