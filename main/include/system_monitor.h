#pragma once

#include <stddef.h>
#include <stdint.h>

/* Every field is 8 bits, so the struct has no padding and no member needs an
   alignment the compiler has to work around. Units are chosen to keep the range:
   SRAM in 4 KB blocks (1 MB), PSRAM free in 1/16 MB steps (15.9 MB), uptime in
   hours (10 days), temperature in whole degrees, signed because it goes below
   zero. A packed attribute on wider fields would also remove the padding, but
   GCC then has to emit byte-wise accesses for member reads. */
#define SYSTEM_TEMP_UNAVAILABLE INT8_MIN

typedef struct {
    uint8_t sram_total_kb4;
    uint8_t sram_free_kb4;
    uint8_t psram_total_mb;
    uint8_t psram_free_16th;
    uint8_t wasm_load_ms;
    uint8_t uptime_h;
    int8_t core_temp_c;
    uint8_t cpu0_pct;
    uint8_t cpu1_pct;
    uint8_t cpu_avg_pct;
} system_metrics_t;

/* 10 bytes, alignment 1: no padding to remove. */
_Static_assert(sizeof(system_metrics_t) == 10, "system_metrics_t layout changed");

void system_monitor_init(void);
void system_monitor_read(system_metrics_t *out);
