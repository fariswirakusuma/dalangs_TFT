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

### CI packaging

The `Firmware` workflow builds once inside the ESP-IDF container and then uses
the esptool that ships with it, so no second toolchain is downloaded:

- `image-info` parses the application image and fails the job if it is corrupt.
- `merge-bin` produces `build/dalangs_tft-factory.bin`, bootloader and partition
  table included, ready to flash at offset 0.

Both steps take well under a second; the job timeout (40 minutes) exists so a
stuck toolchain download fails instead of holding a runner. The artifact is kept
for 14 days and flashing it needs no offsets:

```sh
esptool --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 dalangs_tft-factory.bin
```

## Automated board smoke test

A self-hosted runner with the board attached takes the artifact that CI just
built and flashes it:

```sh
# first time, on the machine with the board:
#   1. create a runner for this repo and give it the label esp32s3
#   2. make sure esptool is on PATH for the runner's user (~/.espressif export)
#   3. set the repository variable HW_PORT if the board is not on /dev/ttyACM0
./scripts/hw_check.sh build/dalangs_tft-factory.bin
```

`make hw-check` runs the same script locally. It flashes, waits for the USB
device to re-enumerate, watches the serial line for three minutes with
`stty raw` and `timeout`, and fails on any of:

| Check | Failure means |
| --- | --- |
| `assert`, `Guru Meditation`, `CORRUPT`, `share GPIO` | firmware aborted |
| `MAIN: ready:` missing | init never finished |
| `pin audit: ... no conflicts` missing | the wiring audit did not run |
| `CAMERA: first frame` missing | camera init or capture is dead |
| frame counter not growing between samples | capture stalled |
| two `leak watch` samples with a falling number | memory leak |

The job covers boot, wiring, capture and leaks. It cannot judge colour, focus or
how the panel looks, and it cannot press the touch panel: those stay in the
manual checklist below.

## Hardware acceptance

Run these in order after flashing. Record the result in the PR.

1. **Boot**: monitor shows the LCD controller, resolution, camera sensor name,
   and free memory, plus `pin audit: N GPIO in use, no conflicts`. No reboot loop.
2. **Display**: the CPU badge and tab icons are readable, not mirrored, not
   colour inverted.
3. **Touch**: tapping each tab selects it. The pointer lands under the finger.
4. **Sliders**: Brightness, Contrast, and Saturation change the preview live.
5. **FX**: Normal, Grayscale, Sepia, Negative apply, and Reset restores defaults.
6. **Live**: camera preview updates; the crosshair and blinking dot are visible.
7. **Draw**: dragging leaves a continuous line; lifting the finger ends it.
8. **Bench**: SRAM/PSRAM usage, core temperature, wasm load time, and uptime update every second.
9. **Profiler**: SRAM and PSRAM values are plausible; WASM time changes per tab.
10. **Soak**: leave it running for at least 15 minutes; the minute-long
    `leak watch` lines must keep the same free SRAM and PSRAM.
11. **Stress**: switch tabs 20 times. Free SRAM after the last switch should be
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

## Touch debug and calibration

Enable coordinate logging:

```sh
make touch-debug
make build flash monitor
```

Tap the screen and read lines like:

```text
TOUCH: raw=(1820,3510) -> screen=(149,215)
```

Use this to set `TOUCH_SWAP_XY`, `TOUCH_MIRROR_X`, `TOUCH_MIRROR_Y`, and the
`TOUCH_RAW_*` window in `pins.h`. The display is natively 320x240 with `MX`
enabled, so `TOUCH_MIRROR_X` must match it. Turn logging off with:

```sh
make touch-debug-off
make build flash monitor
```

## Wasm smoke test

Load every embedded applet once at boot and log its load time:

```sh
make wasm-smoke
make build flash monitor
```

Expected output includes one line per applet:

```text
WASM: basic_sliders loaded in 3 ms, 3 widgets
WASM: fx_buttons loaded in 3 ms, 5 widgets
...
MAIN: wasm smoke 0: ok (3 ms)
```

Turn it off with:

```sh
make wasm-smoke-off
make build flash monitor
```

## Camera-off UI test

If the display works but the screen turns to noise after boot, the camera
preview may be the garbage, not the LCD:

```sh
make camera-off
make build flash monitor
```

With the camera disabled the applet panel and tab bar render on a black
background. If that UI is clean, the LCD is fine and the camera path needs
debugging. Restore it with:

```sh
make camera-on
make build flash monitor
```

## LCD color-bar self test

To separate a panel/wiring fault from an application fault:

```sh
make lcd-selftest
make build flash monitor
```

The panel loops red, green, blue, white, black, and gray full-screen fills
forever before LVGL starts. If every color is clean for several cycles, the
panel, power, and SPI wiring are good. If they are noise or only partly filled,
fix the wiring/power before debugging the UI. Turn it off with:

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
- Compare a declarative applet (sliders) with a native applet (bench).

For a README screenshot, use the `BENCH` or `SLIDERS` tab so the status bar and
the applet content are both visible.
