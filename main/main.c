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
static void firmware_audit(void) {
    ESP_LOGI(TAG, "heap integrity: %s", heap_caps_check_integrity_all(true) ? "OK" : "CORRUPT");
    ESP_LOGI(TAG, "heap: %u KB internal free, %u KB PSRAM free",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

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
