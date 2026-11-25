#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

IMPORT(set_cam_param)
extern void set_cam_param(int param_id, int value);

IMPORT(reset_camera)
extern void reset_camera(void);

IMPORT(ui_button)
extern int ui_button(const char *label);

/* sensor_t special_effect_t values. */
static const int s_effects[4] = {0, 2, 6, 1};

__attribute__((export_name("app_init"))) void app_init(void)
{
    ui_button("Normal");
    ui_button("Grayscale");
    ui_button("Sepia");
    ui_button("Negative");
    ui_button("Reset");
}

__attribute__((export_name("app_event"))) void app_event(int widget_id, int value)
{
    (void)value;
    if (widget_id >= 0 && widget_id < 4) {
        set_cam_param(3, s_effects[widget_id]);
    } else if (widget_id == 4) {
        reset_camera();
    }
}
