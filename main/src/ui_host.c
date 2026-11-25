#include "ui_host.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "camera_tuner.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "pins.h"
#include "system_monitor.h"
#include "ui_model.h"
#include "wasm_host.h"

static const char *TAG = "UI";

#define COLOR_BG 0x0B1220
#define COLOR_SURFACE 0x111A2E
#define COLOR_TEXT 0xE2E8F0
#define COLOR_MUTED 0x94A3B8
#define COLOR_ACCENT 0x38BDF8
#define COLOR_DARK 0x0B1220
#define COLOR_CHROMA 0xF81F

static lv_obj_t *s_root;
static lv_obj_t *s_camera_img;
static lv_obj_t *s_canvas;
static lv_obj_t *s_status_label;
static lv_obj_t *s_panel;
static lv_obj_t *s_content;
static lv_obj_t *s_tabs[APPLET_COUNT];
static lv_timer_t *s_tick_timer;
static lv_img_dsc_t s_camera_desc;
static uint8_t *s_canvas_buf;
static uint32_t s_last_frame_id;
static int s_applet = -1;

static const char *const s_tab_labels[APPLET_COUNT] = {
    [APPLET_SLIDERS] = "SLIDERS",
    [APPLET_FX] = "FX",
    [APPLET_LIVE_CAM] = "LIVE",
    [APPLET_DRAW_PAD] = "DRAW",
    [APPLET_BOUNCE] = "BALL",
};

bool ui_host_applet_uses_canvas(int applet_id)
{
    return applet_id >= APPLET_LIVE_CAM && applet_id < APPLET_COUNT;
}

static lv_obj_t *make_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void style_tab(int applet_id, bool selected)
{
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

static void tab_event_cb(lv_event_t *event)
{
    int applet_id = (int)(intptr_t)lv_event_get_user_data(event);
    if (applet_id != s_applet) {
        ui_host_switch_applet(applet_id);
    }
}

static void slider_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    int widget_id = (int)(intptr_t)lv_obj_get_user_data(slider);
    int value = lv_slider_get_value(slider);

    lv_obj_t *row = lv_obj_get_parent(slider);
    lv_obj_t *value_label = lv_obj_get_child(row, 2);
    if (value_label != NULL) {
        lv_label_set_text_fmt(value_label, "%d", value);
    }

    wasm_host_event(widget_id, value);
}

static void button_event_cb(lv_event_t *event)
{
    int widget_id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(event));
    wasm_host_event(widget_id, 1);
}

static void add_slider_row(const ui_widget_t *widget, int widget_id)
{
    lv_obj_t *row = make_plain(s_content);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_width(name, 78);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(name, widget->label);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_width(slider, 160);
    lv_obj_set_height(slider, 10);
    lv_slider_set_range(slider, widget->min, widget->max);
    lv_slider_set_value(slider, widget->default_value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_user_data(slider, (void *)(intptr_t)widget_id);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *value = lv_label_create(row);
    lv_obj_set_width(value, 30);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(COLOR_ACCENT), 0);
    lv_label_set_text_fmt(value, "%d", widget->default_value);
}

static void add_button(const ui_widget_t *widget, int widget_id, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *button = lv_btn_create(s_content);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x334155), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_user_data(button, (void *)(intptr_t)widget_id);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(label, widget->label);
    lv_obj_center(label);
}

static void render_model(void)
{
    lv_obj_clean(s_content);

    int count = ui_model_count();
    if (count <= 0) {
        lv_obj_t *label = lv_label_create(s_content);
        lv_label_set_text(label, "Applet has no widgets");
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
        return;
    }

    bool all_buttons = true;
    for (int i = 0; i < count; i++) {
        const ui_widget_t *widget = ui_model_get(i);
        if (widget == NULL || widget->type != UI_WIDGET_BUTTON) {
            all_buttons = false;
            break;
        }
    }

    if (all_buttons) {
        lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(s_content, 4, 0);

        for (int i = 0; i < count; i++) {
            const ui_widget_t *widget = ui_model_get(i);
            bool is_reset = strcmp(widget->label, "Reset") == 0;
            add_button(widget, i, is_reset ? 300 : 148, is_reset ? 24 : 34);
        }
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
            add_button(widget, i, 300, 30);
        }
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char text[64];
    system_monitor_format(text, sizeof(text));
    ui_host_set_status(text);
}

static void camera_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_camera_img == NULL) {
        return;
    }

    uint32_t frame_id = camera_tuner_frame_id();
    if (frame_id != s_last_frame_id) {
        s_last_frame_id = frame_id;
        /* shortcut: the camera task writes the same buffer LVGL reads; tearing is
           possible under load. Upgrade to a double buffer if it becomes visible. */
        lv_obj_invalidate(s_camera_img);
    }
}

static void tick_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_applet >= 0) {
        wasm_host_tick(33);
    }
}

static void canvas_event_cb(lv_event_t *event)
{
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

void ui_host_canvas_clear(int color565)
{
    if (s_canvas_buf == NULL) {
        return;
    }
    uint16_t *pixels = (uint16_t *)s_canvas_buf;
    uint16_t color = (uint16_t)color565;
    for (size_t i = 0; i < (size_t)UI_H_RES * UI_V_RES; i++) {
        pixels[i] = color;
    }
}

void ui_host_canvas_rect(int x, int y, int w, int h, int color565)
{
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
    uint16_t color = (uint16_t)color565;
    for (int row = (int)y0; row < (int)y1; row++) {
        uint16_t *line = &pixels[row * UI_H_RES];
        for (int col = (int)x0; col < (int)x1; col++) {
            line[col] = color;
        }
    }
}

static void put_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= UI_H_RES || y < 0 || y >= UI_V_RES) {
        return;
    }
    ((uint16_t *)s_canvas_buf)[y * UI_H_RES + x] = color;
}

void ui_host_canvas_line(int x0, int y0, int x1, int y1, int color565)
{
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
    uint16_t color = (uint16_t)color565;

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

void ui_host_canvas_flush(void)
{
    if (s_canvas != NULL) {
        lv_obj_invalidate(s_canvas);
    }
}

void ui_host_set_status(const char *text)
{
    if (s_status_label != NULL && text != NULL) {
        lv_label_set_text(s_status_label, text);
    }
}

void ui_host_show_message(const char *text)
{
    if (s_content == NULL || text == NULL) {
        return;
    }
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(s_content);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *label = lv_label_create(s_content);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
}

void ui_host_switch_applet(int applet_id)
{
    if (applet_id < 0 || applet_id >= APPLET_COUNT || s_content == NULL) {
        return;
    }

    s_applet = applet_id;

    for (int i = 0; i < APPLET_COUNT; i++) {
        style_tab(i, i == applet_id);
    }

    bool canvas = ui_host_applet_uses_canvas(applet_id);
    if (canvas) {
        if (s_canvas != NULL) {
            lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        ui_host_canvas_clear(COLOR_CHROMA);
        ui_host_canvas_flush();
    } else {
        if (s_canvas != NULL) {
            lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clean(s_content);

    esp_err_t err = wasm_host_load(applet_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "applet %d failed: %s", applet_id, wasm_host_last_error());
        ui_host_show_message(wasm_host_last_error());
        return;
    }

    if (!canvas) {
        render_model();
    }
}

esp_err_t ui_host_init(void)
{
    size_t canvas_bytes = (size_t)UI_H_RES * UI_V_RES * sizeof(uint16_t);
    s_canvas_buf = heap_caps_malloc(canvas_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_canvas_buf == NULL) {
        ESP_LOGW(TAG, "no PSRAM for the %u-byte canvas; canvas applets will be blank",
                 (unsigned)canvas_bytes);
    } else {
        uint16_t *pixels = (uint16_t *)s_canvas_buf;
        for (size_t i = 0; i < (size_t)UI_H_RES * UI_V_RES; i++) {
            pixels[i] = COLOR_CHROMA;
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

    if (s_canvas_buf != NULL) {
        s_canvas = lv_canvas_create(s_root);
        lv_canvas_set_buffer(s_canvas, s_canvas_buf, UI_H_RES, UI_V_RES, LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED);
        lv_obj_set_pos(s_canvas, 0, 0);
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_canvas, canvas_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *status = lv_obj_create(s_root);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, UI_H_RES, UI_STATUS_BAR_H);
    lv_obj_set_pos(status, 0, 0);
    lv_obj_set_style_bg_color(status, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(status, LV_OPA_60, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(status);
    lv_obj_set_pos(s_status_label, 4, 2);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_MUTED), 0);
    lv_label_set_text(s_status_label, "booting...");

    s_panel = lv_obj_create(s_root);
    lv_obj_set_size(s_panel, UI_H_RES - 2 * UI_PANEL_MARGIN, UI_PANEL_H);
    lv_obj_set_pos(s_panel, UI_PANEL_MARGIN, UI_PANEL_Y);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_panel, 230, 0);
    lv_obj_set_style_radius(s_panel, 8, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 6, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_content = make_plain(s_panel);
    lv_obj_set_size(s_content, lv_pct(100), lv_pct(100));

    lv_obj_t *tabbar = make_plain(s_root);
    lv_obj_set_size(tabbar, UI_H_RES, UI_TAB_BAR_H);
    lv_obj_set_pos(tabbar, 0, UI_V_RES - UI_TAB_BAR_H);
    lv_obj_set_flex_flow(tabbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabbar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tabbar, 1, 0);
    lv_obj_set_style_pad_column(tabbar, 2, 0);

    for (int i = 0; i < APPLET_COUNT; i++) {
        lv_obj_t *tab = lv_btn_create(tabbar);
        lv_obj_set_size(tab, 60, 21);
        lv_obj_set_style_radius(tab, 4, 0);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        lv_obj_set_style_border_width(tab, 0, 0);
        lv_obj_set_user_data(tab, (void *)(intptr_t)i);
        lv_obj_add_event_cb(tab, tab_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *label = lv_label_create(tab);
        lv_label_set_text(label, s_tab_labels[i]);
        lv_obj_center(label);
        s_tabs[i] = tab;
        style_tab(i, false);
    }

    s_tick_timer = lv_timer_create(tick_timer_cb, 33, NULL);
    lv_timer_create(camera_timer_cb, 100, NULL);
    lv_timer_create(status_timer_cb, 1000, NULL);
    status_timer_cb(NULL);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "native UI ready");
    return ESP_OK;
}
