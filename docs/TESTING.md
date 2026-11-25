# Testing

## Host tests

No board or ESP-IDF is needed:

```sh
make test
```

This compiles and runs:

- `test/test_cam_params.c` — defaults, clamping, unknown parameter rejection.
- `test/test_ui_model.c` — widget ids, range swap, label truncation, overflow.
- `test/test_wasm_abi.c` — loads every committed applet with wasm3, links the
  real host signatures to stubs, calls `app_init`, `app_event`, `app_tick`, and
  `app_touch`, and checks the resulting widget and parameter calls.
- `test/check_wasm.py` — validates every committed `.wasm`: magic, memory
  export, required exports, allowed imports, and a 64 KB size budget.

CI runs the same target.

## Firmware build

```sh
make build
```

The build must complete with no project-source warnings. Third-party warnings
from LVGL or wasm3 are not actionable here.

## Hardware acceptance

Run these in order after flashing. Record the result in the PR.

1. **Boot**: monitor shows the LCD controller, resolution, camera sensor name,
   and free memory. No reboot loop.
2. **Display**: status bar text is readable, not mirrored, not color-inverted.
3. **Touch**: tapping each tab selects it. The pointer lands under the finger.
4. **Sliders**: Brightness, Contrast, and Saturation change the preview live.
5. **FX**: Normal, Grayscale, Sepia, Negative apply, and Reset restores defaults.
6. **Live**: camera preview updates; the crosshair and blinking dot are visible.
7. **Draw**: dragging leaves a continuous line; lifting the finger ends it.
8. **Ball**: the ball bounces inside the screen and on the green floor.
9. **Profiler**: SRAM and PSRAM values are plausible; WASM time changes per tab.
10. **Stress**: switch tabs 20 times. Free SRAM after the last switch should be
    within a few KB of the first switch. Watch for a slow downward trend.

## Troubleshooting

| Symptom | First check |
| --- | --- |
| White or black screen | LCD controller choice, backlight pin |
| Garbage/static on screen | Wire RESET to IO0, lower `LCD_PIXEL_CLOCK_HZ`, check DC/CS, try `LCD_SPI_MODE 3` |
| Colors inverted | `DALANGS_LCD_INVERT_COLOR`, `LCD_RGB_ORDER_BGR` |
| Image shifted or wrapped | Wrong controller, reduce `LCD_PIXEL_CLOCK_HZ` |
| Touch X/Y swapped | `TOUCH_SWAP_XY` |
| Touch mirrored | `TOUCH_MIRROR_X` / `TOUCH_MIRROR_Y` |
| Touch edges off | `TOUCH_RAW_*` calibration |
| Camera init fails | FPC orientation, 3.3 V, `pins.h` DVP profile |
| Preview striped | PCLK/VSYNC wiring, lower XCLK to 10 MHz |
| Wasm load fails | Rebuild with `make wasm`, reflash; check monitor message |
| Tab switch resets | Brownout; check USB power and backlight current |

## LCD color-bar self test

To separate a panel/wiring fault from an application fault:

```sh
make lcd-selftest
make build flash monitor
```

The panel shows red, green, blue, white, black, and gray full-screen fills
before LVGL starts. If the colors are clean, the panel and SPI wiring are good.
If they are noise or all white, fix the wiring/controller before debugging the
UI. Turn it off with:

```sh
make lcd-selftest-off
make build flash monitor
```

## Measuring

The status bar is the primary instrument:

```text
SRAM 240K  PSRAM 7.8M  WASM 3ms
```

`WASM` is measured from copying the embedded binary to the end of `app_init`.
It is a whole-switch cost, not a steady-state frame cost. Useful comparisons:

- Record free SRAM after boot, then after 20 tab switches.
- Compare load time across applets of different sizes.
- Compare a declarative applet (sliders) with a canvas applet (ball).

For a README screenshot, use the `BALL` or `SLIDERS` tab so the status bar and
the applet content are both visible.
