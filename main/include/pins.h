#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "sdkconfig.h"

/**
 * @file pins.h
 * @brief Single source of truth for the board wiring.
 *
 * Board: ESP32-S3-CAM N16R8 (dual USB-C, onboard microSD and camera FPC).
 * This is the common GOOUUU/DIYables/Freenove-style board with an
 * ESP32-S3-WROOM-1 N16R8 and an OV2640/OV3660/OV5640 DVP camera.
 *
 * Camera pins match the board's DVP header (same as CAMERA_MODEL_ESP32S3_EYE)
 * and are NOT sensor-specific: OV5640 uses the same DVP wiring as OV2640.
 *
 * Reserved by this board and deliberately avoided below:
 *   38/39/40  onboard microSD (SD_CMD / SD_CLK / SD_D0)
 *   48        onboard WS2812 RGB LED
 *   19/20     native USB
 *   43/44     UART0 console
 *   33-37     octal PSRAM
 *
 * The SPI LCD header intentionally uses IO0 (BOOT/RST), IO45 (strapping DC),
 * and IO21 (backlight); those are the board's documented LCD pins.
 *
 * The LCD and touch share one SPI bus. XPT2046 T_CLK/T_DIN are wired to the
 * LCD SCK/MOSI, T_DO goes to LCD_PIN_MISO, and T_CS is separate.
 */

/* =========================================================================
 * Display (SPI2 / FSPI)
 * ========================================================================= */

#define LCD_HOST SPI2_HOST

/*
 * These follow the board's dedicated SPI LCD header, so a ribbon cable or
 * the labeled header pins can be used directly:
 *   GND, VCC, SCL, SDA, RES, DC, CS, BLK
 */
#define LCD_PIN_SCK 42
#define LCD_PIN_MOSI 41
#define LCD_PIN_MISO 14
#define LCD_PIN_CS 47
#define LCD_PIN_DC 45
#define LCD_PIN_RST 0
#define LCD_PIN_BCKL 21

/* Native panel resolution. The UI is rotated to landscape. */
#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_LANDSCAPE 1

#if LCD_LANDSCAPE
#define UI_H_RES LCD_V_RES
#define UI_V_RES LCD_H_RES
#else
#define UI_H_RES LCD_H_RES
#define UI_V_RES LCD_V_RES
#endif

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8

/* Most 2.8 inch IPS modules use BGR order. Swap if red and blue are reversed. */
#define LCD_RGB_ORDER_BGR 1

/* Some panels need a horizontal/vertical mirror after rotation. */
#define LCD_MIRROR_X 0
#define LCD_MIRROR_Y 0

/* =========================================================================
 * Touch (XPT2046, shares LCD SPI bus)
 * ========================================================================= */

/*
 * The LCD header has no touch pins. XPT2046 T_CLK/T_DIN share LCD SCK/MOSI,
 * T_DO uses IO14 (same as LCD MISO), and T_CS uses IO2 (QWIIC SCL).
 */
#define TOUCH_PIN_CS 2

/* XPT2046 PENIRQ is optional; the driver polls over SPI when this is -1. */
#define TOUCH_PIN_IRQ (-1)
#define TOUCH_SPI_CLOCK_HZ (1 * 1000 * 1000)

/* Landscape rotation: swap native X/Y so the touch matches the rotated panel. */
#define TOUCH_SWAP_XY 1
#define TOUCH_MIRROR_X 0
#define TOUCH_MIRROR_Y 0

/*
 * Raw 12-bit calibration window. Sample the four corners and tighten these
 * values if the edges do not reach the screen border. Keep them inside the
 * 50..4045 range accepted by the driver.
 */
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3900
#define TOUCH_RAW_Y_MIN 200
#define TOUCH_RAW_Y_MAX 3900

/* =========================================================================
 * Camera (DVP, generic ESP32-S3-CAM profile)
 * ========================================================================= */

#define CAM_PIN_PWDN (-1)
#define CAM_PIN_RESET (-1)
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5

#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11

#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

#define CAM_XCLK_HZ (20 * 1000 * 1000)
#define CAM_FRAME_W 320
#define CAM_FRAME_H 240

/* Set to 1 if the preview colors are swapped on your sensor/module. */
#define CAMERA_BYTE_SWAP 0

/* =========================================================================
 * Reserved / avoid
 * =========================================================================
 * GPIO 19/20 : native USB
 * GPIO 26-32 : flash
 * GPIO 33-37 : octal PSRAM on N16R8
 * GPIO 43/44 : UART0 console
 * GPIO 45/46 : strapping
 * GPIO 0/3   : strapping
 */
