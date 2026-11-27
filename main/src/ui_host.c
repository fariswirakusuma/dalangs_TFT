#include "ui_host.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "camera_tuner.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "fonts.h"
#include "pins.h"
#include "system_monitor.h"
#include "ui_model.h"
#include "wasm_host.h"

static const char *TAG = "UI";

/* The canvas copies the camera frame straight in, so both must be 1:1. */
_Static_assert(UI_H_RES == CAM_FRAME_W && UI_V_RES == CAM_FRAME_H,
               "canvas and camera frame must match pixel for pixel");

#define COLOR_BG 0x05080F
#define COLOR_SURFACE 0x101827
#define COLOR_TEXT 0xFFFFFF
#define COLOR_MUTED 0xCBD5E1
#define COLOR_ACCENT 0x38BDF8
#define COLOR_DARK 0x0B1220
#define COLOR_CHROMA 0x07E0
#define COLOR_TRACK 0x334155
#define COLOR_LINE 0x1E293B
#define RADIUS_SURFACE 8
#define RADIUS_CONTROL 6

static lv_obj_t *s_root;
static lv_obj_t *s_camera_img;
static lv_obj_t *s_scrim;
static lv_obj_t *s_canvas;
static lv_obj_t *s_panel;
static lv_obj_t *s_content;
static lv_obj_t *s_tabs[APPLET_COUNT];
static lv_obj_t *s_panel_toggle;
static lv_obj_t *s_cpu_label;
static bool s_panel_hidden_by_user;
static lv_timer_t *s_tick_timer;
static lv_obj_t *s_bench_sram;
static lv_obj_t *s_bench_psram;
static lv_obj_t *s_bench_temp;
static lv_obj_t *s_bench_cpu;
static lv_obj_t *s_bench_wasm;
static lv_obj_t *s_bench_uptime;
#if CONFIG_DALANGS_TOUCH_DEBUG
static lv_obj_t *s_touch_marker;
#endif
static lv_img_dsc_t s_camera_desc;
static uint8_t *s_canvas_buf;
static uint32_t s_last_frame_id;
static int s_applet = -1;

/* Icons are LVGL's bundled Font Awesome glyphs; the tab titles do not fit at
   320 px next to them, so the name lives in the status bar instead. */
static const char *const s_tab_icons[APPLET_COUNT] = {
    [APPLET_SLIDERS] = LV_SYMBOL_SETTINGS, [APPLET_FX] = LV_SYMBOL_IMAGE,
    [APPLET_LIVE_CAM] = LV_SYMBOL_VIDEO,   [APPLET_DRAW_PAD] = LV_SYMBOL_EDIT,
    [APPLET_BENCH] = LV_SYMBOL_LIST,
};

bool ui_host_applet_uses_canvas(int applet_id) {
    return applet_id == APPLET_LIVE_CAM || applet_id == APPLET_DRAW_PAD;
}

static lv_obj_t *make_plain(lv_obj_t *parent);

static lv_obj_t *bench_add_row(const char *name, lv_obj_t **value_out) {
    lv_obj_t *row = make_plain(s_content);
    lv_obj_set_size(row, lv_pct(100), 18);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, &roboto_14, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(COLOR_MUTED), 0);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, "--");
    lv_obj_set_style_text_font(value_label, &roboto_md_16, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(COLOR_TEXT), 0);
    *value_out = value_label;
    return row;
}

static void update_bench(void) {
    if (s_applet != APPLET_BENCH || s_bench_sram == NULL) {
        return;
    }

    system_metrics_t m;
    system_monitor_read(&m);

    unsigned sram_total = (unsigned)m.sram_total_kb4 * 4;
    unsigned sram_used = ((unsigned)m.sram_total_kb4 - m.sram_free_kb4) * 4;
    unsigned psram_total = (unsigned)m.psram_total_mb;
    unsigned psram_used16 =
        psram_total * 16 > m.psram_free_16th ? psram_total * 16 - m.psram_free_16th : 0;

    lv_label_set_text_fmt(s_bench_sram, "%u / %u KB", sram_used, sram_total);
    char buf[40];
    snprintf(buf, sizeof(buf), "%u.%u / %u MB", psram_used16 / 16, (psram_used16 % 16) * 10 / 16,
             psram_total);
    lv_label_set_text(s_bench_psram, buf);
    if (m.core_temp_cdeg == SYSTEM_TEMP_UNAVAILABLE) {
        lv_label_set_text(s_bench_temp, "n/a");
    } else {
        int cdeg = m.core_temp_cdeg;
        int mag = cdeg < 0 ? -cdeg : cdeg;
        lv_label_set_text_fmt(s_bench_temp, "%s%d.%02d C", cdeg < 0 ? "-" : "", mag / 100,
                              mag % 100);
    }
    unsigned cpu_avg = ((unsigned)m.cpu0_pct + m.cpu1_pct) / 2;
    lv_label_set_text_fmt(s_bench_cpu, "%u / %u / %u %%", (unsigned)m.cpu0_pct,
                          (unsigned)m.cpu1_pct, cpu_avg);
    lv_label_set_text_fmt(s_bench_wasm, "%u ms", (unsigned)m.wasm_load_ms);
    lv_label_set_text_fmt(s_bench_uptime, "%u h", (unsigned)m.uptime_h);
}

static void render_bench(void) {
    lv_obj_clean(s_content);
    s_bench_sram = NULL;
    s_bench_psram = NULL;
    s_bench_temp = NULL;
    s_bench_cpu = NULL;
    s_bench_wasm = NULL;
    s_bench_uptime = NULL;

    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_content, 2, 0);

    bench_add_row("SRAM", &s_bench_sram);
    bench_add_row("PSRAM", &s_bench_psram);
    bench_add_row("CORE TEMP", &s_bench_temp);
    bench_add_row("CPU0/1/AVG", &s_bench_cpu);
    bench_add_row("WASM", &s_bench_wasm);
    bench_add_row("UPTIME", &s_bench_uptime);
    update_bench();
}

static lv_obj_t *make_plain(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void style_tab(int applet_id, bool selected) {
    lv_obj_t *tab = s_tabs[applet_id];
    if (tab == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(tab, lv_color_hex(selected ? COLOR_ACCENT : COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(tab, selected ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_t *label = lv_obj_get_child(tab, 0);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, lv_color_hex(selected ? COLOR_DARK : COLOR_MUTED), 0);
    }
}

/* The camera image, the scrim over it and the canvas must never be painted when
   they cannot be seen: compositing 320x240 twice per frame costs real CPU.
   show_camera means "the preview is what the user wants to look at". */
static void update_preview_layers(bool canvas) {
    bool show_camera = !canvas && s_panel_hidden_by_user;

    if (s_camera_img != NULL) {
        if (show_camera) {
            lv_obj_clear_flag(s_camera_img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_camera_img, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_scrim != NULL) {
        if (show_camera) {
            lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void panel_toggle_cb(lv_event_t *event) {
    (void)event;
    s_panel_hidden_by_user = !s_panel_hidden_by_user;
    bool canvas = ui_host_applet_uses_canvas(s_applet);
    if (canvas || s_panel_hidden_by_user) {
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }

    update_preview_layers(canvas);

    lv_obj_t *label = lv_obj_get_child(s_panel_toggle, 0);
    if (label != NULL) {
        lv_label_set_text(label, s_panel_hidden_by_user ? "SHOW" : "HIDE");
    }
}

static void tab_event_cb(lv_event_t *event) {
    int applet_id = (int)(intptr_t)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
#if CONFIG_DALANGS_TOUCH_DEBUG
    ESP_LOGI(TAG, "tab event id=%d code=%d", applet_id, code);
#endif
    if (code == LV_EVENT_CLICKED && applet_id != s_applet) {
        ui_host_switch_applet(applet_id);
    }
}

static void slider_event_cb(lv_event_t *event) {
    lv_obj_t *slider = lv_event_get_target(event);
    int widget_id = (int)(intptr_t)lv_obj_get_user_data(slider);
    int value = lv_slider_get_value(slider);

    lv_obj_t *row = lv_obj_get_parent(slider);
    lv_obj_t *value_label = lv_obj_get_child(row, 2);
    if (value_label != NULL) {
        lv_label_set_text_fmt(value_label, "%d", value);
    }

#if CONFIG_DALANGS_TOUCH_DEBUG
    ESP_LOGI(TAG, "slider %d = %d", widget_id, value);
#endif
    wasm_host_event(widget_id, value);
}

/* Keeps exactly one button in the checked style so the applet shows which mode
   it is in (the FX presets, for example). */
static void set_active_button(lv_obj_t *clicked) {
    if (s_content == NULL) {
        return;
    }
    uint32_t count = lv_obj_get_child_cnt(s_content);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(s_content, i);
        if (child == clicked) {
            lv_obj_add_state(child, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(child, LV_STATE_CHECKED);
        }
    }
}

static void button_event_cb(lv_event_t *event) {
    lv_obj_t *button = lv_event_get_target(event);
    int widget_id = (int)(intptr_t)lv_obj_get_user_data(button);
    set_active_button(button);
    wasm_host_event(widget_id, 1);
}

static void add_slider_row(const ui_widget_t *widget, int widget_id) {
    lv_obj_t *row = make_plain(s_content);
    lv_obj_set_size(row, lv_pct(100), 38);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_width(name, 88);
    lv_obj_set_style_text_font(name, &roboto_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(name, widget->label);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_width(slider, 150);
    lv_obj_set_height(slider, 12);
    lv_slider_set_range(slider, widget->min, widget->max);
    lv_slider_set_value(slider, widget->default_value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_BG), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_radius(slider, RADIUS_CONTROL, LV_PART_MAIN);
    lv_obj_set_user_data(slider, (void *)(intptr_t)widget_id);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *value = lv_label_create(row);
    lv_obj_set_width(value, 34);
    lv_obj_set_style_text_font(value, &roboto_md_16, 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(COLOR_ACCENT), 0);
    lv_label_set_text_fmt(value, "%d", widget->default_value);
}

static void add_button(const ui_widget_t *widget, int widget_id, lv_coord_t width,
                       lv_coord_t height) {
    lv_obj_t *button = lv_btn_create(s_content);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x334155), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_hex(COLOR_DARK), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(COLOR_ACCENT), LV_STATE_PRESSED);
    /* Checked = the mode this applet is currently in. */
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(button, lv_color_hex(COLOR_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(button, lv_color_hex(COLOR_DARK), LV_STATE_CHECKED);
    lv_obj_set_user_data(button, (void *)(intptr_t)widget_id);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &roboto_md_16, 0);
    lv_label_set_text(label, widget->label);
    lv_obj_center(label);
}

static void render_model(void) {
    lv_obj_clean(s_content);

    int count = ui_model_count();
    if (count <= 0) {
        lv_obj_t *label = lv_label_create(s_content);
        lv_label_set_text(label, "Applet has no widgets");
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
        return;
    }

    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_content, 2, 0);

    for (int i = 0; i < count; i++) {
        const ui_widget_t *widget = ui_model_get(i);
        if (widget->type == UI_WIDGET_SLIDER) {
            add_slider_row(widget, i);
        } else {
            add_button(widget, i, lv_pct(100), 38);
        }
    }

    /* Rebuilding keeps the old scroll offset, which opened FX halfway down. */
    lv_obj_scroll_to(s_content, 0, 0, LV_ANIM_OFF);

    lv_obj_t *first = lv_obj_get_child(s_content, 0);
    const lv_font_t *first_font = NULL;
    if (first != NULL) {
        lv_obj_t *inner = lv_obj_get_child(first, 0);
        if (inner != NULL) {
            first_font = lv_obj_get_style_text_font(inner, 0);
        }
    }
    ESP_LOGI(TAG, "layout: n=%d box=%dx%d scroll=%d..%d font_ok=%d", count,
             (int)lv_obj_get_content_width(s_content), (int)lv_obj_get_content_height(s_content),
             (int)lv_obj_get_scroll_top(s_content), (int)lv_obj_get_scroll_bottom(s_content),
             first_font == &roboto_16);
}

static void status_timer_cb(lv_timer_t *timer) {
    (void)timer;

#if CONFIG_DALANGS_FIRMWARE_AUDIT
    /* A flat pair of numbers minute after minute is the cheapest leak test:
       anything that allocates per frame shows up here. */
    static uint32_t s_audit_tick;
    if (++s_audit_tick % 60 == 0) {
        system_metrics_t m;
        system_monitor_read(&m);
        int cdeg = m.core_temp_cdeg;
        int mag = cdeg < 0 ? -cdeg : cdeg;
        ESP_LOGI(TAG,
                 "leak watch: internal %u KB free, PSRAM %u KB free, cpu %u/%u avg %u%%, "
                 "temp %s%d.%02d C",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned)m.cpu0_pct, (unsigned)m.cpu1_pct,
                 (unsigned)(((unsigned)m.cpu0_pct + m.cpu1_pct) / 2), cdeg < 0 ? "-" : "",
                 mag / 100, mag % 100);
    }
#endif

    if (s_cpu_label != NULL) {
        system_metrics_t m;
        system_monitor_read(&m);
        lv_label_set_text_fmt(s_cpu_label, "CPU %u%%",
                              (unsigned)(((unsigned)m.cpu0_pct + m.cpu1_pct) / 2));
    }

    update_bench();
}

static void camera_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_camera_img == NULL) {
        return;
    }

    uint32_t frame_id = camera_tuner_frame_id();
    if (frame_id != s_last_frame_id) {
        s_last_frame_id = frame_id;
        /* The preview double buffers, so follow the front buffer and force LVGL
           to reopen the image after the swap. */
        s_camera_desc.data = camera_tuner_frame_buffer();
        lv_img_cache_invalidate_src(&s_camera_desc);
        lv_img_set_src(s_camera_img, &s_camera_desc);
    }
}

#if CONFIG_DALANGS_TOUCH_DEBUG
static void touch_marker_timer_cb(lv_timer_t *timer) {
    (void)timer;
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev == NULL || s_touch_marker == NULL) {
        return;
    }

    if (indev->proc.state == LV_INDEV_STATE_PRESSED) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        lv_obj_set_pos(s_touch_marker, point.x - 5, point.y - 5);
        lv_obj_clear_flag(s_touch_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_touch_marker, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

static void tick_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_applet >= 0) {
        wasm_host_tick(33);
    }
}

static void canvas_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        wasm_host_touch(point.x, point.y, true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        wasm_host_touch(point.x, point.y, false);
    }
}

static uint16_t canvas_color(int color565) {
    /* Accept both historical transparent colors (magenta and green) and map
       them to LVGL's actual chroma key, whatever it is configured to. */
    if (color565 == 0xF81F || color565 == 0x07E0) {
        return lv_color_chroma_key().full;
    }

    uint8_t r = (uint8_t)((color565 >> 11) & 0x1F);
    uint8_t g = (uint8_t)((color565 >> 5) & 0x3F);
    uint8_t b = (uint8_t)(color565 & 0x1F);
    lv_color_t color = lv_color_make((uint8_t)(r << 3), (uint8_t)(g << 2), (uint8_t)(b << 3));
    return color.full;
}

void ui_host_canvas_clear(int color565) {
    if (s_canvas_buf == NULL) {
        return;
    }

    /* Applets use the chroma color to mean "show what is behind me"; composite the
       live camera frame there so the canvas itself is the camera layer. */
    const uint8_t *frame = camera_tuner_frame_buffer();
    if (color565 == COLOR_CHROMA && frame != NULL) {
        memcpy(s_canvas_buf, frame, (size_t)UI_H_RES * UI_V_RES * 2);
        return;
    }

    uint16_t *pixels = (uint16_t *)s_canvas_buf;
    /* 0x08A4 is COLOR_BG expressed as RGB565, used when there is no camera. */
    uint16_t color = canvas_color(color565 == COLOR_CHROMA ? 0x08A4 : color565);
    for (size_t i = 0; i < (size_t)UI_H_RES * UI_V_RES; i++) {
        pixels[i] = color;
    }
}

void ui_host_canvas_rect(int x, int y, int w, int h, int color565) {
    if (s_canvas_buf == NULL || w <= 0 || h <= 0) {
        return;
    }

    int64_t x0 = x < 0 ? 0 : x;
    int64_t y0 = y < 0 ? 0 : y;
    int64_t x1 = (int64_t)x + w;
    int64_t y1 = (int64_t)y + h;
    if (x1 > UI_H_RES) {
        x1 = UI_H_RES;
    }
    if (y1 > UI_V_RES) {
        y1 = UI_V_RES;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    uint16_t *pixels = (uint16_t *)s_canvas_buf;
    uint16_t color = canvas_color(color565);
    for (int row = (int)y0; row < (int)y1; row++) {
        uint16_t *line = &pixels[row * UI_H_RES];
        for (int col = (int)x0; col < (int)x1; col++) {
            line[col] = color;
        }
    }
}

static void put_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= UI_H_RES || y < 0 || y >= UI_V_RES) {
        return;
    }
    ((uint16_t *)s_canvas_buf)[y * UI_H_RES + x] = color;
}

void ui_host_canvas_line(int x0, int y0, int x1, int y1, int color565) {
    if (s_canvas_buf == NULL) {
        return;
    }

    /* Clamp wild endpoints from an applet before the Bresenham arithmetic. */
    x0 = x0 < -1024 ? -1024 : (x0 > UI_H_RES + 1024 ? UI_H_RES + 1024 : x0);
    y0 = y0 < -1024 ? -1024 : (y0 > UI_V_RES + 1024 ? UI_V_RES + 1024 : y0);
    x1 = x1 < -1024 ? -1024 : (x1 > UI_H_RES + 1024 ? UI_H_RES + 1024 : x1);
    y1 = y1 < -1024 ? -1024 : (y1 > UI_V_RES + 1024 ? UI_V_RES + 1024 : y1);

    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    uint16_t color = canvas_color(color565);

    for (int guard = 0; guard < 2048; guard++) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ui_host_canvas_flush(void) {
    if (s_canvas != NULL) {
        lv_obj_invalidate(s_canvas);
    }
}

void ui_host_show_message(const char *text) {
    if (s_content == NULL || text == NULL) {
        return;
    }
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(s_content);
    s_bench_sram = NULL;
    s_bench_psram = NULL;
    s_bench_temp = NULL;
    s_bench_cpu = NULL;
    s_bench_wasm = NULL;
    s_bench_uptime = NULL;
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *label = lv_label_create(s_content);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_font(label, &roboto_14, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
}

void ui_host_switch_applet(int applet_id) {
    assert(applet_id >= 0 && applet_id < APPLET_COUNT);
    if (applet_id < 0 || applet_id >= APPLET_COUNT || s_content == NULL) {
        return;
    }

    s_applet = applet_id;

    for (int i = 0; i < APPLET_COUNT; i++) {
        style_tab(i, i == applet_id);
    }

    bool canvas = ui_host_applet_uses_canvas(applet_id);
    update_preview_layers(canvas);
    if (s_scrim != NULL) {
        bool covered = !canvas && !s_panel_hidden_by_user;
        if (covered) {
            lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (canvas) {
        if (s_canvas != NULL) {
            lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        ui_host_canvas_clear(COLOR_CHROMA);
        ui_host_canvas_flush();
    } else {
        if (s_canvas != NULL) {
            lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        }
    }

    bool hide_panel = canvas || s_panel_hidden_by_user;
    if (hide_panel) {
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_panel_toggle != NULL) {
        if (canvas) {
            lv_obj_add_flag(s_panel_toggle, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_panel_toggle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t *label = lv_obj_get_child(s_panel_toggle, 0);
            if (label != NULL) {
                lv_label_set_text(label, s_panel_hidden_by_user ? "SHOW" : "HIDE");
            }
        }
    }

    lv_obj_clean(s_content);
    s_bench_sram = NULL;
    s_bench_psram = NULL;
    s_bench_temp = NULL;
    s_bench_cpu = NULL;
    s_bench_wasm = NULL;
    s_bench_uptime = NULL;

    if (applet_id == APPLET_BENCH) {
        esp_err_t err = wasm_host_load(applet_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "applet %d failed: %s", applet_id, wasm_host_last_error());
            ui_host_show_message(wasm_host_last_error());
            return;
        }
        render_bench();
        ESP_LOGI(TAG, "applet %d canvas=%d panel_hidden=%d tab_y=%d", applet_id, canvas,
                 lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN), UI_V_RES - UI_TAB_BAR_H);
        return;
    }

    esp_err_t err = wasm_host_load(applet_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "applet %d failed: %s", applet_id, wasm_host_last_error());
        ui_host_show_message(wasm_host_last_error());
        return;
    }

    if (!canvas) {
        render_model();
    }

    if (!canvas) {
        lv_obj_fade_in(s_panel, 150, 0);
    }

    ESP_LOGI(TAG, "applet %d canvas=%d panel_hidden=%d widgets=%d tab_y=%d", applet_id, canvas,
             lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN), ui_model_count(),
             UI_V_RES - UI_TAB_BAR_H);
}

esp_err_t ui_host_init(void) {
    size_t canvas_bytes = (size_t)UI_H_RES * UI_V_RES * sizeof(uint16_t);
    s_canvas_buf = heap_caps_malloc(canvas_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_canvas_buf == NULL) {
        ESP_LOGW(TAG, "no PSRAM for the %u-byte canvas; canvas applets will be blank",
                 (unsigned)canvas_bytes);
    } else {
        uint16_t *pixels = (uint16_t *)s_canvas_buf;
        uint16_t clear = canvas_color(COLOR_CHROMA);
        for (size_t i = 0; i < (size_t)UI_H_RES * UI_V_RES; i++) {
            pixels[i] = clear;
        }
    }

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    s_root = lv_scr_act();
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    const uint8_t *frame = camera_tuner_frame_buffer();
    if (frame != NULL && camera_tuner_frame_size() >= (size_t)UI_H_RES * UI_V_RES * 2) {
        memset(&s_camera_desc, 0, sizeof(s_camera_desc));
        s_camera_desc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_camera_desc.header.w = UI_H_RES;
        s_camera_desc.header.h = UI_V_RES;
        s_camera_desc.data_size = (uint32_t)UI_H_RES * UI_V_RES * 2;
        s_camera_desc.data = frame;

        s_camera_img = lv_img_create(s_root);
        lv_img_set_src(s_camera_img, &s_camera_desc);
        lv_obj_set_pos(s_camera_img, 0, 0);
        lv_obj_clear_flag(s_camera_img, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* The camera sits behind the whole UI and its brightness bled through the
       cards, so every tab outside LIVE and DRAW gets an opaque surface. HIDE
       drops it when the preview is what you want to look at. */
    s_scrim = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, UI_H_RES, UI_V_RES);
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_IGNORE_LAYOUT);

    if (s_canvas_buf != NULL) {
        s_canvas = lv_canvas_create(s_root);
        lv_canvas_set_buffer(s_canvas, s_canvas_buf, UI_H_RES, UI_V_RES, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(s_canvas, 0, 0);
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_canvas, canvas_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    s_panel = lv_obj_create(s_root);
    lv_obj_set_size(s_panel, UI_H_RES - 2 * UI_PANEL_MARGIN, UI_PANEL_H);
    lv_obj_set_pos(s_panel, UI_PANEL_MARGIN, UI_PANEL_Y);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_panel, 225, 0);
    lv_obj_set_style_radius(s_panel, RADIUS_SURFACE, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_pad_all(s_panel, 8, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_content = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, lv_pct(100), lv_pct(100));
    /* Applets such as FX have more widgets than fit, so the content scrolls;
       the bar is 10 px wide so a finger can drag it. */
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(s_content, 10, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_content, 5, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_content, lv_color_hex(COLOR_ACCENT), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_content, 140, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(s_content, 12, 0);

    lv_obj_t *tabbar = make_plain(s_root);
    lv_obj_set_size(tabbar, UI_H_RES, UI_TAB_BAR_H);
    lv_obj_set_pos(tabbar, 0, UI_V_RES - UI_TAB_BAR_MARGIN_BOTTOM - UI_TAB_BAR_H);
    lv_obj_set_style_bg_color(tabbar, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(tabbar, 225, 0);
    lv_obj_set_style_radius(tabbar, RADIUS_SURFACE, 0);
    lv_obj_set_style_border_width(tabbar, 1, 0);
    lv_obj_set_style_border_color(tabbar, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_flex_flow(tabbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabbar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tabbar, 1, 0);
    lv_obj_set_style_pad_column(tabbar, 2, 0);

    for (int i = 0; i < APPLET_COUNT; i++) {
        lv_obj_t *tab = lv_btn_create(tabbar);
        lv_obj_set_size(tab, 56, UI_TAB_BAR_H - 2);
        lv_obj_set_style_radius(tab, RADIUS_CONTROL, 0);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        lv_obj_set_style_border_width(tab, 0, 0);
        lv_obj_set_user_data(tab, (void *)(intptr_t)i);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(tab, 8);
        lv_obj_add_event_cb(tab, tab_event_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
        lv_obj_t *label = lv_label_create(tab);
        lv_label_set_text(label, s_tab_icons[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_center(label);
        s_tabs[i] = tab;
        style_tab(i, false);
    }

#if CONFIG_DALANGS_TOUCH_DEBUG
    s_touch_marker = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_touch_marker);
    lv_obj_set_size(s_touch_marker, 10, 10);
    lv_obj_set_style_radius(s_touch_marker, 5, 0);
    lv_obj_set_style_bg_color(s_touch_marker, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(s_touch_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_touch_marker, 0, 0);
    lv_obj_clear_flag(s_touch_marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_touch_marker, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(touch_marker_timer_cb, 50, NULL);
#endif

    s_panel_toggle = lv_btn_create(s_root);
    lv_obj_set_size(s_panel_toggle, 56, 24);
    lv_obj_set_pos(s_panel_toggle, UI_H_RES - 60, 22);
    lv_obj_set_style_radius(s_panel_toggle, RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_color(s_panel_toggle, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_panel_toggle, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_panel_toggle, 0, 0);
    lv_obj_set_style_border_width(s_panel_toggle, 0, 0);
    lv_obj_set_ext_click_area(s_panel_toggle, 8);
    lv_obj_add_event_cb(s_panel_toggle, panel_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *toggle_label = lv_label_create(s_panel_toggle);
    lv_label_set_text(toggle_label, "HIDE");
    lv_obj_set_style_text_font(toggle_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(toggle_label, lv_color_hex(COLOR_DARK), 0);
    lv_obj_center(toggle_label);

    lv_obj_move_foreground(s_panel);
    lv_obj_move_foreground(tabbar);
    lv_obj_move_foreground(s_panel_toggle);
    /* A small readout in the corner: the one number worth watching live. */
    lv_obj_t *cpu_badge = lv_obj_create(s_root);
    lv_obj_remove_style_all(cpu_badge);
    lv_obj_set_size(cpu_badge, 64, 18);
    lv_obj_align(cpu_badge, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(cpu_badge, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(cpu_badge, 220, 0);
    lv_obj_set_style_radius(cpu_badge, 6, 0);
    lv_obj_set_style_border_width(cpu_badge, 1, 0);
    lv_obj_set_style_border_color(cpu_badge, lv_color_hex(COLOR_LINE), 0);

    s_cpu_label = lv_label_create(cpu_badge);
    lv_label_set_text(s_cpu_label, "CPU --%");
    lv_obj_set_style_text_font(s_cpu_label, &roboto_14, 0);
    lv_obj_set_style_text_color(s_cpu_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_center(s_cpu_label);

    s_tick_timer = lv_timer_create(tick_timer_cb, 33, NULL);
    lv_timer_create(camera_timer_cb, 100, NULL);
    lv_timer_create(status_timer_cb, 1000, NULL);
    status_timer_cb(NULL);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "LVGL mem: %u%% used, %u bytes free, %u%% frag", mon.used_pct,
             (unsigned)mon.free_size, mon.frag_pct);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "native UI ready");
    return ESP_OK;
}
