#include "system_monitor.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wasm_host.h"

static const char *TAG = "MONITOR";
static temperature_sensor_handle_t s_temp_sensor;
static uint64_t s_prev_cpu_us;
static uint64_t s_prev_busy_us[2];
static uint8_t s_last_cpu[3];

void system_monitor_init(void) {
    if (s_temp_sensor != NULL) {
        return;
    }

    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t err = temperature_sensor_install(&cfg, &s_temp_sensor);
    if (err != ESP_OK) {
        s_temp_sensor = NULL;
        ESP_LOGW(TAG, "core temperature sensor unavailable: %s", esp_err_to_name(err));
        return;
    }

    err = temperature_sensor_enable(s_temp_sensor);
    ESP_LOGI(TAG, "core temperature sensor %s", err == ESP_OK ? "ready" : "failed");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "core temperature sensor enable failed: %s", esp_err_to_name(err));
        temperature_sensor_uninstall(s_temp_sensor);
        s_temp_sensor = NULL;
    }
}

void system_monitor_read(system_metrics_t *out) {
    if (out == NULL) {
        return;
    }

    out->sram_total = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    out->sram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    out->psram_total = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    out->psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->wasm_load_ms = wasm_host_last_load_ms();
    int64_t now_us = esp_timer_get_time();
    out->uptime_s = (uint32_t)(now_us / 1000000);
    out->core_temp_c = NAN;

    if (s_prev_cpu_us != 0 && (uint64_t)now_us > s_prev_cpu_us &&
        (uint64_t)now_us - s_prev_cpu_us >= 500000) {
        TaskStatus_t tasks[48];
        UBaseType_t count = uxTaskGetSystemState(tasks, 48, NULL);
        uint64_t busy[2] = {0, 0};
        for (UBaseType_t i = 0; i < count; i++) {
            if (tasks[i].pcTaskName == NULL || strncmp(tasks[i].pcTaskName, "IDLE", 4) == 0) {
                continue;
            }
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
            int core = (int)tasks[i].xCoreID;
            if (core == 0 || core == 1) {
                busy[core] += (uint64_t)tasks[i].ulRunTimeCounter;
            }
#else
            busy[0] += (uint64_t)tasks[i].ulRunTimeCounter;
#endif
        }

        uint64_t elapsed = (uint64_t)now_us - s_prev_cpu_us;
        for (int core = 0; core < 2; core++) {
            uint64_t delta =
                busy[core] > s_prev_busy_us[core] ? busy[core] - s_prev_busy_us[core] : 0;
            uint32_t pct = (uint32_t)((delta * 100) / elapsed);
            s_last_cpu[core] = (uint8_t)(pct > 100 ? 100 : pct);
        }
        s_last_cpu[2] = (uint8_t)((s_last_cpu[0] + s_last_cpu[1]) / 2);
        s_prev_cpu_us = (uint64_t)now_us;
        s_prev_busy_us[0] = busy[0];
        s_prev_busy_us[1] = busy[1];
    } else if (s_prev_cpu_us == 0) {
        s_prev_cpu_us = (uint64_t)now_us;
        s_prev_busy_us[0] = 0;
        s_prev_busy_us[1] = 0;
    }

    out->cpu0_pct = s_last_cpu[0];
    out->cpu1_pct = s_last_cpu[1];
    out->cpu_avg_pct = s_last_cpu[2];

    if (s_temp_sensor != NULL) {
        float celsius = 0;
        esp_err_t err = temperature_sensor_get_celsius(s_temp_sensor, &celsius);
        if (err == ESP_OK) {
            out->core_temp_c = celsius;
        } else {
            static bool logged;
            if (!logged) {
                ESP_LOGW(TAG, "core temperature read failed: %s", esp_err_to_name(err));
                logged = true;
            }
        }
    }
}

void system_monitor_format(char *out, size_t out_len) {
    if (out == NULL || out_len == 0) {
        return;
    }

    system_metrics_t m;
    system_monitor_read(&m);

    if (m.psram_total > 0) {
        snprintf(out, out_len, "SRAM %uK  PSRAM %.1fM  WASM %ums", (unsigned)(m.sram_free / 1024),
                 (double)m.psram_free / (1024.0 * 1024.0), (unsigned)m.wasm_load_ms);
    } else {
        snprintf(out, out_len, "SRAM %uK  PSRAM --  WASM %ums", (unsigned)(m.sram_free / 1024),
                 (unsigned)m.wasm_load_ms);
    }
}
