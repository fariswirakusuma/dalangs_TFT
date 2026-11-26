#include <assert.h>

#include "app_config.h"
#include "board_display.h"
#include "board_touch.h"
#include "camera_tuner.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_monitor.h"
#include "ui_host.h"
#include "wasm_host.h"

static const char *TAG = "MAIN";

/*
 * Every RTOS task this firmware creates is spawned here so stack size, priority
 * and core affinity sit next to app_main and the boot audit can be checked
 * against one place. Modules expose a *_task() entry point and never spawn.
 */
#define CAMERA_TASK_STACK 4096
#define CAMERA_TASK_PRIORITY 3
#define CAMERA_TASK_CORE 1 /* core 0 keeps the LVGL task and the wasm ticks */

static StaticTask_t s_camera_tcb;
static StackType_t s_camera_stack[CAMERA_TASK_STACK];
static TaskHandle_t s_camera_task;

static void start_camera_task(void) {
    if (s_camera_task != NULL || !camera_tuner_ready()) {
        return;
    }
    s_camera_task = xTaskCreateStaticPinnedToCore(camera_tuner_task, "camera", CAMERA_TASK_STACK,
                                                  NULL, CAMERA_TASK_PRIORITY, s_camera_stack,
                                                  &s_camera_tcb, CAMERA_TASK_CORE);
    if (s_camera_task == NULL) {
        ESP_LOGE(TAG, "camera task creation failed");
    }
}

#if CONFIG_DALANGS_FIRMWARE_AUDIT
/*
 * Wiring audit: every GPIO the firmware drives, in one table. A duplicate is a
 * configuration bug that silently breaks two devices, and a reserved pin means
 * the module itself would be disturbed, so both assert instead of warning.
 */
typedef struct {
    const char *name;
    int pin;
} pin_ref_t;

static const pin_ref_t s_pin_map[] = {
    {"LCD_SCK", LCD_PIN_SCK},     {"LCD_MOSI", LCD_PIN_MOSI}, {"LCD_MISO", LCD_PIN_MISO},
    {"LCD_CS", LCD_PIN_CS},       {"LCD_DC", LCD_PIN_DC},     {"LCD_RST", LCD_PIN_RST},
    {"LCD_BCKL", LCD_PIN_BCKL},   {"TOUCH_CS", TOUCH_PIN_CS}, {"TOUCH_IRQ", TOUCH_PIN_IRQ},
    {"CAM_XCLK", CAM_PIN_XCLK},   {"CAM_SIOD", CAM_PIN_SIOD}, {"CAM_SIOC", CAM_PIN_SIOC},
    {"CAM_VSYNC", CAM_PIN_VSYNC}, {"CAM_HREF", CAM_PIN_HREF}, {"CAM_PCLK", CAM_PIN_PCLK},
    {"CAM_D0", CAM_PIN_D0},       {"CAM_D1", CAM_PIN_D1},     {"CAM_D2", CAM_PIN_D2},
    {"CAM_D3", CAM_PIN_D3},       {"CAM_D4", CAM_PIN_D4},     {"CAM_D5", CAM_PIN_D5},
    {"CAM_D6", CAM_PIN_D6},       {"CAM_D7", CAM_PIN_D7},
};

static void pin_audit(void) {
    static const int reserved[] = BOARD_RESERVED_PINS;
    size_t count = sizeof(s_pin_map) / sizeof(s_pin_map[0]);
    int used = 0;

    for (size_t i = 0; i < count; i++) {
        int pin = s_pin_map[i].pin;
        if (pin < 0) {
            continue;
        }
        used++;

        for (size_t r = 0; r < sizeof(reserved) / sizeof(reserved[0]); r++) {
            if (pin == reserved[r]) {
                ESP_LOGE(TAG, "pin audit: %s uses GPIO %d, reserved by the module",
                         s_pin_map[i].name, pin);
                assert(false);
            }
        }
        for (size_t j = i + 1; j < count; j++) {
            if (s_pin_map[j].pin == pin) {
                ESP_LOGE(TAG, "pin audit: %s and %s share GPIO %d", s_pin_map[i].name,
                         s_pin_map[j].name, pin);
                assert(false);
            }
        }
    }
    ESP_LOGI(TAG, "pin audit: %d GPIO in use, no conflicts", used);
}

static void firmware_audit(void) {
    ESP_LOGI(TAG, "heap integrity: %s", heap_caps_check_integrity_all(true) ? "OK" : "CORRUPT");
    ESP_LOGI(TAG,
             "heap: internal %u KB free (largest block %u KB), PSRAM %u KB free "
             "(largest block %u KB)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));

    TaskStatus_t tasks[48];
    UBaseType_t count = uxTaskGetSystemState(tasks, 48, NULL);
    for (UBaseType_t i = 0; i < count; i++) {
        if (tasks[i].pcTaskName != NULL) {
            ESP_LOGI(TAG, "task %-14s core=%d prio=%u stack_hi=%u", tasks[i].pcTaskName,
                     (int)tasks[i].xCoreID, (unsigned)tasks[i].uxCurrentPriority,
                     (unsigned)tasks[i].usStackHighWaterMark);
        }
    }

#if LCD_PIN_BCKL >= 0
    ESP_LOGI(TAG, "pins: LCD CS=%d DC=%d RST=%d BL=%d", gpio_get_level(LCD_PIN_CS),
             gpio_get_level(LCD_PIN_DC), gpio_get_level(LCD_PIN_RST), gpio_get_level(LCD_PIN_BCKL));
#else
    ESP_LOGI(TAG, "pins: LCD CS=%d DC=%d RST=%d", gpio_get_level(LCD_PIN_CS),
             gpio_get_level(LCD_PIN_DC), gpio_get_level(LCD_PIN_RST));
#endif
    ESP_LOGI(TAG, "pins: TOUCH CS=%d", gpio_get_level(TOUCH_PIN_CS));
}
#endif

void app_main(void) {
    ESP_LOGI(TAG, "%s %s starting", APP_NAME, APP_VERSION);

    pin_audit();

    ESP_ERROR_CHECK(board_display_init());
    ESP_ERROR_CHECK(board_touch_init(board_display_handle()));

#if CONFIG_DALANGS_CAMERA_ENABLE
    esp_err_t camera_err = camera_tuner_init();
    if (camera_err != ESP_OK) {
        ESP_LOGW(TAG, "continuing without camera preview");
    }
#else
    esp_err_t camera_err = ESP_ERR_NOT_SUPPORTED;
    (void)camera_err;
    ESP_LOGW(TAG, "camera preview disabled by config");
#endif

    ESP_ERROR_CHECK(ui_host_init());
    ESP_ERROR_CHECK(wasm_host_init());
    system_monitor_init();

#if CONFIG_DALANGS_WASM_SMOKE_TEST
    if (lvgl_port_lock(0)) {
        for (int i = 0; i < APPLET_COUNT; i++) {
            esp_err_t err = wasm_host_load(i);
            ESP_LOGI(TAG, "wasm smoke %d: %s (%u ms)", i,
                     err == ESP_OK ? "ok" : wasm_host_last_error(),
                     (unsigned)wasm_host_last_load_ms());
        }
        lvgl_port_unlock();
    }
#endif

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "LVGL lock failed during first applet load");
        return;
    }
    ui_host_switch_applet(APPLET_SLIDERS);
#if CONFIG_DALANGS_CAMERA_ENABLE
    if (camera_err != ESP_OK) {
        ui_host_show_message("Camera init failed. Check the DVP cable and pins.h.");
    }
#endif
    lvgl_port_unlock();

#if CONFIG_DALANGS_CAMERA_ENABLE
    if (camera_err == ESP_OK) {
        start_camera_task();
    }
#endif

    ESP_LOGI(TAG, "ready: %s, %u KB SRAM free, %u KB PSRAM free", camera_tuner_sensor_name(),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

#if CONFIG_DALANGS_FIRMWARE_AUDIT
    firmware_audit();
#endif
}
