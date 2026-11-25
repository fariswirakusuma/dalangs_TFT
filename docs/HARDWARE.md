# Hardware

## Board

- **MCU**: ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM).
- **Camera**: OV2640 or OV5640 DVP module, generic ESP32-S3-CAM pinout.
- **Display**: 2.8 inch 240x320 SPI TFT, ST7789 or ILI9341.
- **Touch**: XPT2046 resistive controller.

The red 2.8 inch modules with a 14-pin header are frequently **ILI9341**.
Black modules are frequently **ST7789**. Confirm the controller before
blaming the firmware. The driver is selected at build time:

```sh
make menuconfig
# dalangs_TFT -> LCD controller -> ST7789 / ILI9341
# dalangs_TFT -> Invert panel colors
```

## Pin map

All values are in [`main/include/pins.h`](../main/include/pins.h). Change them
there and update this table in the same commit.

### Display (SPI2 / FSPI)

| Signal | GPIO | Notes |
| --- | --- | --- |
| SCK | 21 | Shared with XPT2046 `T_CLK` |
| MOSI (SDA) | 14 | Shared with XPT2046 `T_DIN` |
| MISO | 39 | Shared with XPT2046 `T_DO` |
| CS | 47 | Display chip select |
| DC | 1 | Data/command |
| RST | 2 | Panel reset |
| BL | 38 | Backlight enable, active high |

### Touch (XPT2046)

| Signal | GPIO | Notes |
| --- | --- | --- |
| T_CS | 41 | Separate chip select |
| T_IRQ | 40 | Optional; set to `-1` in `pins.h` if unused |
| T_CLK | 21 | Shared with LCD SCK |
| T_DIN | 14 | Shared with LCD MOSI |
| T_DO | 39 | Shared with LCD MISO |

### Camera (DVP)

| Signal | GPIO | Signal | GPIO |
| --- | --- | --- | --- |
| XCLK | 15 | VSYNC | 6 |
| SIOD (SDA) | 4 | HREF | 7 |
| SIOC (SCL) | 5 | PCLK | 13 |
| D0 | 11 | D4 | 12 |
| D1 | 9 | D5 | 18 |
| D2 | 8 | D6 | 17 |
| D3 | 10 | D7 | 16 |
| PWDN | -1 | RESET | -1 |

`PWDN` and `RESET` are tied off on the common camera module.

## Reserved pins

| GPIO | Why to avoid |
| --- | --- |
| 19, 20 | Native USB D-/D+ |
| 26-32 | SPI flash |
| 33-37 | Octal PSRAM on N16R8 |
| 43, 44 | UART0 console |
| 45, 46 | Strapping |
| 0, 3 | Strapping |

GPIO 39-42 are also JTAG pins. This build uses 39-41 for the shared SPI bus, so
use the USB-Serial-JTAG console rather than an external JTAG probe while the
display is connected.

## Power and signal notes

- The panel and XPT2046 are **3.3 V** devices. Do not apply 5 V to logic pins.
- Backlight current depends on the module. Use a transistor or a series resistor
  sized for the panel; a GPIO is a control signal, not a power source.
- Keep the camera FPC away from the SPI clock line. Long unshielded wiring on
  PCLK, VSYNC, and HREF is the usual cause of a striped or missing preview.
- Use a short, solid ground return between the TFT module and the ESP32-S3.
- If the display works but touch does not, check `T_CS` and `T_DO` first. The
  touch controller shares SCK/MOSI with the display but has its own CS.

## Touch calibration

The driver is configured to pass raw 12-bit values through. `board_touch.c`
maps them to the panel and rotates them to landscape.

1. Flash a sketch or use the `DRAW` tab.
2. Touch each corner and note where the pointer lands.
3. Adjust in `pins.h`:
   - `TOUCH_RAW_X_MIN`, `TOUCH_RAW_X_MAX`
   - `TOUCH_RAW_Y_MIN`, `TOUCH_RAW_Y_MAX`
   - `TOUCH_SWAP_XY`, `TOUCH_MIRROR_X`, `TOUCH_MIRROR_Y`
4. Rebuild and repeat. Keep raw values inside `50..4045`.

## Display orientation

The UI is 320x240 landscape, rotated from the native 240x320 panel. The rotation
is applied by `esp_lvgl_port` from `LCD_LANDSCAPE`. If the image is upside down,
set `LCD_MIRROR_X` or `LCD_MIRROR_Y`; if the image is sideways, check that
`LCD_LANDSCAPE` is still `1`.

## Camera

The firmware requests `QVGA` (320x240) in `RGB565` with two PSRAM frame buffers.
This avoids JPEG decoding and matches the screen size. The sensor is detected by
PID and logged at boot. A missing camera does not crash the UI; the preview stays
black and the tuner still responds.

If the image is mirrored or upside down, change the `set_vflip` / `set_hmirror`
calls in `camera_tuner.c`. They are orientation settings, not wiring faults.

If the preview has red and blue swapped while the native UI colors are correct,
set `CAMERA_BYTE_SWAP` to `1` in `pins.h` and rebuild. Some OV5640 modules output
the opposite RGB565 byte order.
