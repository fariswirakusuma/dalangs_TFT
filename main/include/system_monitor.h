#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sram_total;
    uint32_t sram_free;
    uint32_t psram_total;
    uint32_t psram_free;
    uint32_t wasm_load_ms;
    uint32_t uptime_s;
    float core_temp_c;
    uint8_t cpu0_pct;
    uint8_t cpu1_pct;
    uint8_t cpu_avg_pct;
} system_metrics_t;

void system_monitor_init(void);
void system_monitor_read(system_metrics_t *out);

/* Compact status-bar string: SRAM, PSRAM, wasm load time. */
void system_monitor_format(char *out, size_t out_len);
