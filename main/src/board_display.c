#include "board_display.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
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
static void show_color_bars(esp_lcd_panel_handle_t panel)
{
    static const struct {
        const char *name;
        uint16_t color;
    } steps[] = {
        {"red", 0xF800},
        {"green", 0x07E0},
        {"blue", 0x001F},
        {"white", 0xFFFF},
        {"black", 0x0000},
        {"gray", 0x8410},
    };

    size_t bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    uint16_t *buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGW(TAG, "self-test skipped: no PSRAM buffer");
        return;
    }

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        for (size_t p = 0; p < bytes / sizeof(uint16_t); p++) {
            buffer[p] = steps[i].color;
        }
        ESP_LOGI(TAG, "self-test: %s", steps[i].name);
        esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, buffer);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    heap_caps_free(buffer);
}
#endif

lv_disp_t *board_display_handle(void)
{
    return s_display;
}

const char *board_display_controller_name(void)
{
    return s_controller;
}

static void backlight_on(void)
{
#if LCD_PIN_BCKL >= 0
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_PIN_BCKL,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(LCD_PIN_BCKL, 1);
#endif
}

esp_err_t board_display_init(void)
{
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
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus init failed");

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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io), TAG,
                        "panel io init failed");

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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel), TAG, "ILI9341 create failed");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel), TAG, "ST7789 create failed");
#endif

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset failed");

    /* Software reset covers boards where the hardware RESET wire is missing. */
    esp_lcd_panel_io_tx_param(io, 0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

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
    port_cfg.task_stack = 12288;
    port_cfg.task_priority = 4;
    port_cfg.task_affinity = 0;
    port_cfg.task_max_sleep_ms = 20;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port init failed");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = UI_H_RES * 40,
        .double_buffer = true,
        .hres = UI_H_RES,
        .vres = UI_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = LCD_LANDSCAPE,
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    s_display = lvgl_port_add_disp(&disp_cfg);
    if (s_display == NULL) {
        ESP_LOGE(TAG, "lvgl display registration failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "display ready: %dx%d landscape, controller %s", UI_H_RES, UI_V_RES, s_controller);
    return ESP_OK;
}
