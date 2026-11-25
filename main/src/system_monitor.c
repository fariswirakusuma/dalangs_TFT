#include "system_monitor.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "wasm_host.h"

void system_monitor_format(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    size_t sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (psram > 0) {
        snprintf(out, out_len, "SRAM %uK  PSRAM %.1fM  WASM %ums", (unsigned)(sram / 1024),
                 (double)psram / (1024.0 * 1024.0), (unsigned)wasm_host_last_load_ms());
    } else {
        snprintf(out, out_len, "SRAM %uK  PSRAM --  WASM %ums", (unsigned)(sram / 1024),
                 (unsigned)wasm_host_last_load_ms());
    }
}
