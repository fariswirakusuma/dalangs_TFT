#include "board_touch.h"

#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "pins.h"

static const char *TAG = "TOUCH";
static esp_lcd_touch_handle_t s_touch;

static int clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static int map_axis(uint16_t raw, int raw_min, int raw_max, int out_max)
{
    if (raw_max <= raw_min) {
        return 0;
    }
    int span = raw_max - raw_min;
    int value = ((int)raw - raw_min) * out_max / span;
    return clamp_int(value, 0, out_max);
}

/*
 * The XPT2046 driver is configured to pass raw 12-bit values through, so the
 * calibrated mapping and the landscape rotation live here in one place.
 */
static void touch_process(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                         uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp;
    (void)strength;

#if CONFIG_DALANGS_TOUCH_DEBUG
    static int64_t last_log_us;
    int64_t now_us = esp_timer_get_time();
    bool log_now = (now_us - last_log_us) > 200000;
    if (log_now) {
        last_log_us = now_us;
    }
#endif

    for (uint8_t i = 0; i < *point_num && i < max_point_num; i++) {
        int nx = map_axis(x[i], TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, LCD_H_RES - 1);
        int ny = map_axis(y[i], TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, LCD_V_RES - 1);

#if TOUCH_MIRROR_X
        nx = LCD_H_RES - 1 - nx;
#endif
#if TOUCH_MIRROR_Y
        ny = LCD_V_RES - 1 - ny;
#endif

#if TOUCH_SWAP_XY
        int out_x = ny;
        int out_y = nx;
#else
        int out_x = nx;
        int out_y = ny;
#endif

#if CONFIG_DALANGS_TOUCH_DEBUG
        if (log_now) {
            ESP_LOGI(TAG, "raw=(%u,%u) -> screen=(%d,%d)", x[i], y[i], out_x, out_y);
        }
#endif

        x[i] = (uint16_t)out_x;
        y[i] = (uint16_t)out_y;
    }
}

esp_err_t board_touch_init(lv_disp_t *disp)
{
    if (disp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_PIN_CS);
    io_cfg.pclk_hz = TOUCH_SPI_CLOCK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io), TAG,
                        "touch io init failed");

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = 4095,
        .y_max = 4095,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = TOUCH_PIN_IRQ >= 0 ? (gpio_num_t)TOUCH_PIN_IRQ : GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = touch_process,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(io, &touch_cfg, &s_touch), TAG, "xpt2046 init failed");

    lvgl_port_touch_cfg_t lv_touch_cfg = {
        .disp = disp,
        .handle = s_touch,
        .scale = {
            .x = 1.0f,
            .y = 1.0f,
        },
    };
    if (lvgl_port_add_touch(&lv_touch_cfg) == NULL) {
        ESP_LOGE(TAG, "lvgl touch registration failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "XPT2046 ready on CS=%d IRQ=%d", TOUCH_PIN_CS, TOUCH_PIN_IRQ);
    return ESP_OK;
}
