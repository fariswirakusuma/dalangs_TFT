#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t wasm_host_init(void);

/*
 * Replaces the running applet. Calls app_init() and fills ui_model.
 * The caller must hold the LVGL port lock because the host renders widgets.
 */
esp_err_t wasm_host_load(int applet_id);

void wasm_host_event(int widget_id, int value);
void wasm_host_tick(int elapsed_ms);
void wasm_host_touch(int x, int y, bool pressed);

uint32_t wasm_host_last_load_ms(void);
const char *wasm_host_last_error(void);
