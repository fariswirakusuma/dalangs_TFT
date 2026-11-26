/* Host ABI test: loads the real applets with wasm3 and stubs the imports. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm3.h"

typedef struct {
    int sliders;
    int buttons;
    int cam_calls;
    int resets;
    int canvas_calls;
    int last_param;
    int last_value;
} state_t;

static state_t s_state;

m3ApiRawFunction(stub_log)
{
    m3ApiGetArgMem(const char *, text);
    m3ApiGetArg(uint32_t, len);
    if (text != NULL && len > 0) {
        printf("  applet log: %.*s\n", (int)len, text);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(stub_slider)
{
    m3ApiReturnType(int)
    m3ApiGetArgMem(const char *, label);
    m3ApiGetArg(int, min);
    m3ApiGetArg(int, max);
    m3ApiGetArg(int, def);
    (void)label;
    (void)min;
    (void)max;
    (void)def;
    m3ApiReturn(s_state.sliders++);
}

m3ApiRawFunction(stub_button)
{
    m3ApiReturnType(int)
    m3ApiGetArgMem(const char *, label);
    (void)label;
    m3ApiReturn(s_state.buttons++);
}

m3ApiRawFunction(stub_cam_param)
{
    m3ApiGetArg(int, param);
    m3ApiGetArg(int, value);
    s_state.cam_calls++;
    s_state.last_param = param;
    s_state.last_value = value;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_reset)
{
    s_state.resets++;
    m3ApiSuccess();
}

m3ApiRawFunction(stub_canvas)
{
    s_state.canvas_calls++;
    m3ApiSuccess();
}

static M3Result link_import(IM3Module module, const char *name, const char *sig, M3RawCall fn)
{
    M3Result result = m3_LinkRawFunction(module, "env", name, sig, fn);
    return result == m3Err_functionLookupFailed ? m3Err_none : result;
}

static M3Result link_all(IM3Module module)
{
    M3Result result;
#define LINK(name, sig, fn)                        \
    do {                                           \
        result = link_import(module, name, sig, fn); \
        if (result) {                              \
            return result;                         \
        }                                          \
    } while (0)

    LINK("log", "v(ii)", stub_log);
    LINK("ui_slider", "i(iiii)", stub_slider);
    LINK("ui_button", "i(i)", stub_button);
    LINK("set_cam_param", "v(ii)", stub_cam_param);
    LINK("reset_camera", "v()", stub_reset);
    LINK("canvas_clear", "v(i)", stub_canvas);
    LINK("canvas_rect", "v(iiiii)", stub_canvas);
    LINK("canvas_line", "v(iiiii)", stub_canvas);
    LINK("canvas_flush", "v()", stub_canvas);
#undef LINK
    return m3Err_none;
}

static uint8_t *read_file(const char *path, uint32_t *out_len)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    assert(size > 0);

    uint8_t *data = malloc((size_t)size);
    assert(data != NULL);
    assert(fread(data, 1, (size_t)size, file) == (size_t)size);
    fclose(file);

    *out_len = (uint32_t)size;
    return data;
}

static void run_applet(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    uint32_t size = 0;
    uint8_t *bytes = read_file(path, &size);

    IM3Environment env = m3_NewEnvironment();
    assert(env != NULL);
    IM3Runtime runtime = m3_NewRuntime(env, 16 * 1024, NULL);
    assert(runtime != NULL);

    IM3Module module = NULL;
    M3Result result = m3_ParseModule(env, &module, bytes, size);
    assert(result == NULL);
    result = m3_LoadModule(runtime, module);
    assert(result == NULL);
    result = link_all(module);
    assert(result == NULL);

    memset(&s_state, 0, sizeof(s_state));

    IM3Function init = NULL;
    result = m3_FindFunction(&init, runtime, "app_init");
    assert(result == NULL);
    result = m3_CallV(init);
    printf("%s: init=%s slider=%d button=%d canvas=%d\n", name, result ? result : "ok",
           s_state.sliders, s_state.buttons, s_state.canvas_calls);
    assert(result == NULL);

    if (strcmp(name, "basic_sliders.wasm") == 0) {
        assert(s_state.sliders == 3);
        assert(s_state.buttons == 0);
        IM3Function event = NULL;
        assert(m3_FindFunction(&event, runtime, "app_event") == NULL);
        assert(m3_CallV(event, 1, 2) == NULL);
        assert(s_state.cam_calls == 1);
        assert(s_state.last_param == 1 && s_state.last_value == 2);
    } else if (strcmp(name, "fx_buttons.wasm") == 0) {
        assert(s_state.buttons == 5);
        IM3Function event = NULL;
        assert(m3_FindFunction(&event, runtime, "app_event") == NULL);
        assert(m3_CallV(event, 2, 1) == NULL);
        assert(s_state.last_param == 3 && s_state.last_value == 6);
        assert(m3_CallV(event, 4, 1) == NULL);
        assert(s_state.resets == 1);
    } else if (strcmp(name, "live_cam.wasm") == 0) {
        assert(s_state.canvas_calls >= 2);
        IM3Function tick = NULL;
        assert(m3_FindFunction(&tick, runtime, "app_tick") == NULL);
        assert(m3_CallV(tick, 33) == NULL);
    } else if (strcmp(name, "draw_pad.wasm") == 0) {
        IM3Function touch = NULL;
        assert(m3_FindFunction(&touch, runtime, "app_touch") == NULL);
        assert(m3_CallV(touch, 10, 10, 1) == NULL);
        assert(m3_CallV(touch, 20, 20, 1) == NULL);
        assert(m3_CallV(touch, 20, 20, 0) == NULL);
        assert(s_state.canvas_calls >= 3);
    } else if (strcmp(name, "bench.wasm") == 0) {
        assert(s_state.sliders == 0);
        assert(s_state.buttons == 0);
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free(bytes);
}

int main(void)
{
    const char *dir = getenv("DALANGS_WASM_DIR");
    if (dir == NULL) {
        dir = "main/wasm";
    }

    run_applet(dir, "basic_sliders.wasm");
    run_applet(dir, "fx_buttons.wasm");
    run_applet(dir, "live_cam.wasm");
    run_applet(dir, "draw_pad.wasm");
    run_applet(dir, "bench.wasm");

    puts("test_wasm_abi: ok");
    return 0;
}
