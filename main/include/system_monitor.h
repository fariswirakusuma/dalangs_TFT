#pragma once

#include <stddef.h>
#include <stdint.h>

/* Centi-degrees as int16: the sensor is configured for -10..80 C, so the widest
   value is 8000 and there is no chance of reaching the +/-327 C limit. The other
   fields are single bytes so the struct has no padding: 2 + 8 = 10 bytes on a
   2-byte alignment. SRAM is in 4 KB blocks (1 MB), PSRAM free in 1/16 MB steps
   (15.9 MB), uptime in hours (10 days). The CPU average is the caller's business
   rather than a stored field, which is what keeps the size even. A packed
   attribute would buy nothing here and would force byte-wise member access. */
#define SYSTEM_TEMP_UNAVAILABLE INT16_MIN

typedef struct {
    int16_t core_temp_cdeg;
    uint8_t sram_total_kb4;
    uint8_t sram_free_kb4;
    uint8_t psram_total_mb;
    uint8_t psram_free_16th;
    uint8_t wasm_load_ms;
    uint8_t uptime_h;
    uint8_t cpu0_pct;
    uint8_t cpu1_pct;
} system_metrics_t;

_Static_assert(sizeof(system_metrics_t) == 10, "system_metrics_t layout changed");

void system_monitor_init(void);
void system_monitor_read(system_metrics_t *out);
