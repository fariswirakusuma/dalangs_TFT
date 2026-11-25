#include "wasm_host.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "camera_tuner.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui_host.h"
#include "ui_model.h"
#include "wasm3.h"

static const char *TAG = "WASM";

extern const uint8_t basic_sliders_wasm_start[] asm("_binary_basic_sliders_wasm_start");
extern const uint8_t basic_sliders_wasm_end[] asm("_binary_basic_sliders_wasm_end");
extern const uint8_t fx_buttons_wasm_start[] asm("_binary_fx_buttons_wasm_start");
extern const uint8_t fx_buttons_wasm_end[] asm("_binary_fx_buttons_wasm_end");
extern const uint8_t live_cam_wasm_start[] asm("_binary_live_cam_wasm_start");
extern const uint8_t live_cam_wasm_end[] asm("_binary_live_cam_wasm_end");
extern const uint8_t draw_pad_wasm_start[] asm("_binary_draw_pad_wasm_start");
extern const uint8_t draw_pad_wasm_end[] asm("_binary_draw_pad_wasm_end");
extern const uint8_t bouncing_ball_wasm_start[] asm("_binary_bouncing_ball_wasm_start");
extern const uint8_t bouncing_ball_wasm_end[] asm("_binary_bouncing_ball_wasm_end");

typedef struct {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
} applet_blob_t;

static const applet_blob_t s_applets[APPLET_COUNT] = {
    [APPLET_SLIDERS] = {"basic_sliders", basic_sliders_wasm_start, basic_sliders_wasm_end},
    [APPLET_FX] = {"fx_buttons", fx_buttons_wasm_start, fx_buttons_wasm_end},
    [APPLET_LIVE_CAM] = {"live_cam", live_cam_wasm_start, live_cam_wasm_end},
    [APPLET_DRAW_PAD] = {"draw_pad", draw_pad_wasm_start, draw_pad_wasm_end},
    [APPLET_BOUNCE] = {"bouncing_ball", bouncing_ball_wasm_start, bouncing_ball_wasm_end},
};

static IM3Environment s_env;
static IM3Runtime s_runtime;
static IM3Function s_app_init;
static IM3Function s_app_event;
static IM3Function s_app_tick;
static IM3Function s_app_touch;
static uint8_t *s_wasm_copy;
static uint32_t s_load_ms;
static char s_error[128];

static const char *memory_string(IM3Runtime runtime, void *mem, const char *ptr)
{
    uint32_t size = m3_GetMemorySize(runtime);
    uintptr_t base = (uintptr_t)mem;
    uintptr_t p = (uintptr_t)ptr;

    if (p == 0 || base == 0 || p < base || p >= base + size) {
        return NULL;
    }

    size_t remaining = (size_t)(base + size - p);
    if (strnlen((const char *)p, remaining) == remaining) {
        return NULL;
    }
    return (const char *)p;
}

m3ApiRawFunction(host_log)
{
    m3ApiGetArgMem(const char *, text);
    m3ApiGetArg(uint32_t, len);

    if (text != NULL && len > 0) {
        m3ApiCheckMem(text, len);
        ESP_LOGI(TAG, "applet: %.*s", (int)len, text);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_ui_slider)
{
    m3ApiReturnType(int)
    m3ApiGetArgMem(const char *, label);
    m3ApiGetArg(int, min);
    m3ApiGetArg(int, max);
    m3ApiGetArg(int, default_value);

    const char *safe_label = memory_string(runtime, _mem, label);
    if (safe_label == NULL) {
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    }

    m3ApiReturn(ui_model_add_slider(safe_label, min, max, default_value));
}

m3ApiRawFunction(host_ui_button)
{
    m3ApiReturnType(int)
    m3ApiGetArgMem(const char *, label);

    const char *safe_label = memory_string(runtime, _mem, label);
    if (safe_label == NULL) {
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    }

    m3ApiReturn(ui_model_add_button(safe_label));
}

m3ApiRawFunction(host_set_cam_param)
{
    m3ApiGetArg(int, param_id);
    m3ApiGetArg(int, value);
    camera_tuner_set_param(param_id, value);
    m3ApiSuccess();
}

m3ApiRawFunction(host_reset_camera)
{
    camera_tuner_reset();
    m3ApiSuccess();
}

m3ApiRawFunction(host_canvas_clear)
{
    m3ApiGetArg(int, color);
    ui_host_canvas_clear(color);
    m3ApiSuccess();
}

m3ApiRawFunction(host_canvas_rect)
{
    m3ApiGetArg(int, x);
    m3ApiGetArg(int, y);
    m3ApiGetArg(int, w);
    m3ApiGetArg(int, h);
    m3ApiGetArg(int, color);
    ui_host_canvas_rect(x, y, w, h, color);
    m3ApiSuccess();
}

m3ApiRawFunction(host_canvas_line)
{
    m3ApiGetArg(int, x0);
    m3ApiGetArg(int, y0);
    m3ApiGetArg(int, x1);
    m3ApiGetArg(int, y1);
    m3ApiGetArg(int, color);
    ui_host_canvas_line(x0, y0, x1, y1, color);
    m3ApiSuccess();
}

m3ApiRawFunction(host_canvas_flush)
{
    ui_host_canvas_flush();
    m3ApiSuccess();
}

static M3Result link_import(IM3Module module, const char *name, const char *signature, M3RawCall fn)
{
    M3Result result = m3_LinkRawFunction(module, "env", name, signature, fn);
    if (result == m3Err_functionLookupFailed) {
        return m3Err_none;
    }
    return result;
}

static M3Result link_all(IM3Module module)
{
    M3Result result;
    result = link_import(module, "log", "v(ii)", host_log);
    if (result) {
        return result;
    }
    result = link_import(module, "ui_slider", "i(iiii)", host_ui_slider);
    if (result) {
        return result;
    }
    result = link_import(module, "ui_button", "i(i)", host_ui_button);
    if (result) {
        return result;
    }
    result = link_import(module, "set_cam_param", "v(ii)", host_set_cam_param);
    if (result) {
        return result;
    }
    result = link_import(module, "reset_camera", "v()", host_reset_camera);
    if (result) {
        return result;
    }
    result = link_import(module, "canvas_clear", "v(i)", host_canvas_clear);
    if (result) {
        return result;
    }
    result = link_import(module, "canvas_rect", "v(iiiii)", host_canvas_rect);
    if (result) {
        return result;
    }
    result = link_import(module, "canvas_line", "v(iiiii)", host_canvas_line);
    if (result) {
        return result;
    }
    return link_import(module, "canvas_flush", "v()", host_canvas_flush);
}

static void unload(void)
{
    if (s_runtime != NULL) {
        m3_FreeRuntime(s_runtime);
        s_runtime = NULL;
    }
    s_app_init = NULL;
    s_app_event = NULL;
    s_app_tick = NULL;
    s_app_touch = NULL;

    if (s_wasm_copy != NULL) {
        heap_caps_free(s_wasm_copy);
        s_wasm_copy = NULL;
    }
}

static esp_err_t fail(const char *message)
{
    snprintf(s_error, sizeof(s_error), "%s", message != NULL ? message : "unknown error");
    ESP_LOGE(TAG, "%s", s_error);
    unload();
    return ESP_FAIL;
}

esp_err_t wasm_host_init(void)
{
    s_env = m3_NewEnvironment();
    if (s_env == NULL) {
        return fail("m3_NewEnvironment failed");
    }
    ESP_LOGI(TAG, "wasm3 environment ready");
    return ESP_OK;
}

esp_err_t wasm_host_load(int applet_id)
{
    if (s_env == NULL) {
        return fail("wasm host not initialized");
    }
    if (applet_id < 0 || applet_id >= APPLET_COUNT) {
        return fail("invalid applet id");
    }

    unload();
    s_error[0] = '\0';
    ui_model_reset();

    const applet_blob_t *blob = &s_applets[applet_id];
    uint32_t size = (uint32_t)(blob->end - blob->start);
    if (size == 0) {
        return fail("empty wasm blob");
    }

    s_wasm_copy = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_wasm_copy == NULL) {
        s_wasm_copy = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    if (s_wasm_copy == NULL) {
        return fail("no memory for wasm copy");
    }
    memcpy(s_wasm_copy, blob->start, size);

    int64_t start_us = esp_timer_get_time();

    s_runtime = m3_NewRuntime(s_env, 16 * 1024, NULL);
    if (s_runtime == NULL) {
        return fail("m3_NewRuntime failed");
    }

    IM3Module module = NULL;
    M3Result result = m3_ParseModule(s_env, &module, s_wasm_copy, size);
    if (result) {
        return fail(result);
    }

    result = m3_LoadModule(s_runtime, module);
    if (result) {
        m3_FreeModule(module);
        return fail(result);
    }

    result = link_all(module);
    if (result) {
        return fail(result);
    }

    result = m3_FindFunction(&s_app_init, s_runtime, "app_init");
    if (result) {
        return fail(result);
    }

    /* Optional exports: a missing function is not an error. */
    m3_FindFunction(&s_app_event, s_runtime, "app_event");
    m3_FindFunction(&s_app_tick, s_runtime, "app_tick");
    m3_FindFunction(&s_app_touch, s_runtime, "app_touch");
    m3_ResetErrorInfo(s_runtime);

    result = m3_CallV(s_app_init);
    if (result) {
        return fail(result);
    }

    s_load_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    ESP_LOGI(TAG, "%s loaded in %u ms, %d widgets", blob->name, (unsigned)s_load_ms, ui_model_count());
    return ESP_OK;
}

void wasm_host_event(int widget_id, int value)
{
    if (s_app_event == NULL) {
        return;
    }
    M3Result result = m3_CallV(s_app_event, widget_id, value);
    if (result) {
        ESP_LOGE(TAG, "app_event failed: %s", result);
    }
}

void wasm_host_tick(int elapsed_ms)
{
    if (s_app_tick == NULL) {
        return;
    }
    M3Result result = m3_CallV(s_app_tick, elapsed_ms);
    if (result) {
        ESP_LOGE(TAG, "app_tick failed: %s", result);
        s_app_tick = NULL;
    }
}

void wasm_host_touch(int x, int y, bool pressed)
{
    if (s_app_touch == NULL) {
        return;
    }
    M3Result result = m3_CallV(s_app_touch, x, y, pressed ? 1 : 0);
    if (result) {
        ESP_LOGE(TAG, "app_touch failed: %s", result);
    }
}

uint32_t wasm_host_last_load_ms(void)
{
    return s_load_ms;
}

const char *wasm_host_last_error(void)
{
    return s_error;
}
