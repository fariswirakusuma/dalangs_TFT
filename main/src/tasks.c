#include "tasks.h"

#include "camera_tuner.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TASKS";

/* Capture runs on core 1: the LVGL task and the wasm ticks live on core 0. */
#define CAMERA_TASK_STACK 4096
#define CAMERA_TASK_PRIORITY 3
#define CAMERA_TASK_CORE 1

static StaticTask_t s_camera_tcb;
static StackType_t s_camera_stack[CAMERA_TASK_STACK];
static TaskHandle_t s_camera_task;

void app_tasks_start(void) {
    if (!camera_tuner_ready() || s_camera_task != NULL) {
        return;
    }

    s_camera_task = xTaskCreateStaticPinnedToCore(camera_tuner_task, "camera", CAMERA_TASK_STACK,
                                                  NULL, CAMERA_TASK_PRIORITY, s_camera_stack,
                                                  &s_camera_tcb, CAMERA_TASK_CORE);
    if (s_camera_task == NULL) {
        ESP_LOGE(TAG, "camera task creation failed");
    }
}
