#include "app_config.h"
#include "board_display.h"
#include "board_touch.h"
#include "camera_tuner.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ui_host.h"
#include "wasm_host.h"

static const char *TAG = "MAIN";

void app_main(void)
{
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
        camera_tuner_start();
    }
#endif

    ESP_LOGI(TAG, "ready: %s, %u KB SRAM free, %u KB PSRAM free", camera_tuner_sensor_name(),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}
