#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

esp_err_t ui_host_init(void);

/*
 * Rebuilds the wasm widget area for the selected applet.
 * Caller must hold the LVGL port lock, or call before the LVGL task starts.
 */
void ui_host_switch_applet(int applet_id);

/* Updates the native status bar text (top of the screen). */
void ui_host_set_status(const char *text);

/* Fill the camera image buffer with color bars without starting the camera. */
void ui_host_camera_test_pattern(void);

/* Shows a short native error in the widget panel. */
void ui_host_show_message(const char *text);

bool ui_host_applet_uses_canvas(int applet_id);

/* Canvas drawing imports (called by wasm_host on the LVGL task). */
void ui_host_canvas_clear(int color565);
void ui_host_canvas_rect(int x, int y, int w, int h, int color565);
void ui_host_canvas_line(int x0, int y0, int x1, int y1, int color565);
void ui_host_canvas_flush(void);
