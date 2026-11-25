#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t board_display_init(void);

lv_disp_t *board_display_handle(void);

const char *board_display_controller_name(void);
