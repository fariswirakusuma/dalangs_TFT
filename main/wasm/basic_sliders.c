#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

IMPORT(set_cam_param)
extern void set_cam_param(int param_id, int value);

IMPORT(ui_slider)
extern int ui_slider(const char *label, int min, int max, int default_value);

__attribute__((export_name("app_init"))) void app_init(void) {
    ui_slider("Brightness", -2, 2, 0);
    ui_slider("Contrast", -2, 2, 0);
    ui_slider("Saturation", -2, 2, 0);
}

__attribute__((export_name("app_event"))) void app_event(int widget_id, int value) {
    if (widget_id >= 0 && widget_id <= 2) {
        set_cam_param(widget_id, value);
    }
}
