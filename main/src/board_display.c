#include "board_display.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pins.h"

#ifndef CONFIG_DALANGS_LCD_INVERT_COLOR
#define CONFIG_DALANGS_LCD_INVERT_COLOR 0
#endif

#if CONFIG_DALANGS_LCD_ILI9341
#include "esp_lcd_ili9341.h"
#endif

static const char *TAG = "DISPLAY";
static lv_disp_t *s_display;

#if CONFIG_DALANGS_LCD_ILI9341
static const char *s_controller = "ILI9341";
#else
static const char *s_controller = "ST7789";
#endif

#if CONFIG_DALANGS_LCD_SELF_TEST
static void show_color_bars(esp_lcd_panel_handle_t panel) {
    static const struct {
        const char *name;
        uint16_t color;
    } steps[] = {
        {"red", 0xF800},   {"green", 0x07E0}, {"blue", 0x001F},
        {"white", 0xFFFF}, {"black", 0x0000}, {"gray", 0x8410},
    };

    size_t bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    /* Internal DMA RAM is slower to allocate but avoids PSRAM/DMA quirks. */
    uint16_t *buffer = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (buffer == NULL) {
        ESP_LOGW(TAG, "self-test skipped: no frame buffer");
        return;
    }

    /* If these bottom-only blocks never appear, the lower gate lines/flex are bad.
       If they appear, the full-frame transfer path is losing the tail. */
    for (int test = 0; test < 3; test++) {
        uint16_t color = steps[test].color;
        for (size_t p = 0; p < bytes / sizeof(uint16_t); p++) {
            buffer[p] = color;
        }
        ESP_LOGI(TAG, "self-test: bottom-only %s", steps[test].name);
        esp_lcd_panel_draw_bitmap(panel, 0, LCD_V_RES - 80, LCD_H_RES, LCD_V_RES, buffer);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    for (;;) {
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
            for (size_t p = 0; p < bytes / sizeof(uint16_t); p++) {
                buffer[p] = steps[i].color;
            }
            ESP_LOGI(TAG, "self-test: %s", steps[i].name);
            esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, buffer);
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }
}
#endif

lv_disp_t *board_display_handle(void) {
    return s_display;
}

const char *board_display_controller_name(void) {
    return s_controller;
}

static void backlight_on(void) {
#if LCD_PIN_BCKL >= 0
    /* Full brightness blows this panel out in a dim room, which reads as a hazy
       picture; dim it over PWM instead of switching the pin on. LEDC timer 0 is
       taken by the camera XCLK. */
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel = {
        .gpio_num = LCD_PIN_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
    };
    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&channel) != ESP_OK) {
        gpio_config_t cfg = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << LCD_PIN_BCKL,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        gpio_set_level(LCD_PIN_BCKL, 1);
        return;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 170);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
#endif
}

esp_err_t board_display_init(void) {
    ESP_LOGI(TAG, "%s %dx%d SPI panel on host %d at %d MHz", s_controller, LCD_H_RES, LCD_V_RES,
             LCD_HOST, LCD_PIXEL_CLOCK_HZ / 1000000);

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_PIN_SCK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = UI_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG,
                        "spi bus init failed");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = LCD_SPI_MODE,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io),
                        TAG, "panel io init failed");

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
#if LCD_RGB_ORDER_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };

#if CONFIG_DALANGS_LCD_ILI9341
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel), TAG,
                        "ILI9341 create failed");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel), TAG,
                        "ST7789 create failed");
#endif

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset failed");

    /* Software reset covers boards where the hardware RESET wire is missing. */
    esp_lcd_panel_io_tx_param(io, 0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t panel_id[3] = {0};
    esp_err_t id_err = esp_lcd_panel_io_rx_param(io, 0x04, panel_id, sizeof(panel_id));
    ESP_LOGI(TAG, "panel ID 04 (%s): %02X %02X %02X", esp_err_to_name(id_err), panel_id[0],
             panel_id[1], panel_id[2]);

    uint8_t panel_id4[4] = {0};
    id_err = esp_lcd_panel_io_rx_param(io, 0xD3, panel_id4, sizeof(panel_id4));
    ESP_LOGI(TAG, "panel ID D3 (%s): %02X %02X %02X %02X", esp_err_to_name(id_err), panel_id4[0],
             panel_id4[1], panel_id4[2], panel_id4[3]);

    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, CONFIG_DALANGS_LCD_INVERT_COLOR), TAG,
                        "invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "display on failed");

    backlight_on();

#if CONFIG_DALANGS_LCD_SELF_TEST
    ESP_LOGI(TAG, "drawing color-bar self test");
    show_color_bars(panel);
#endif

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16384;
    port_cfg.task_priority = 4;
    port_cfg.task_affinity = 0;
    port_cfg.task_max_sleep_ms = 20;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port init failed");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * 20,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = LCD_MIRROR_X,
                .mirror_y = LCD_MIRROR_Y,
            },
        .flags =
            {
                .buff_dma = true,
            },
    };
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl lock failed while adding display");
        return ESP_ERR_TIMEOUT;
    }

    s_display = lvgl_port_add_disp(&disp_cfg);
    if (s_display == NULL) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "lvgl display registration failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "rotation=none hor=%d ver=%d sw_rotate=%d", lv_disp_get_hor_res(s_display),
             lv_disp_get_ver_res(s_display), s_display->driver->sw_rotate);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "display ready: %dx%d native, controller %s", UI_H_RES, UI_V_RES, s_controller);
    return ESP_OK;
}
