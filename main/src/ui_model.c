#include "ui_model.h"

#include <stdio.h>
#include <string.h>

static ui_widget_t s_widgets[UI_MODEL_MAX_WIDGETS];
static int s_count;

static void copy_label(char *dst, const char *src) {
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, UI_MODEL_LABEL_LEN, "%s", src);
}

static int add_widget(ui_widget_type_t type, const char *label) {
    if (label == NULL || label[0] == '\0' || s_count >= UI_MODEL_MAX_WIDGETS) {
        return -1;
    }

    ui_widget_t *widget = &s_widgets[s_count];
    memset(widget, 0, sizeof(*widget));
    widget->type = type;
    copy_label(widget->label, label);
    return s_count++;
}

void ui_model_reset(void) {
    memset(s_widgets, 0, sizeof(s_widgets));
    s_count = 0;
}

int ui_model_add_slider(const char *label, int min, int max, int default_value) {
    int id = add_widget(UI_WIDGET_SLIDER, label);
    if (id < 0) {
        return -1;
    }

    if (min > max) {
        int tmp = min;
        min = max;
        max = tmp;
    }

    ui_widget_t *widget = &s_widgets[id];
    widget->min = min;
    widget->max = max;
    widget->default_value = default_value < min ? min : (default_value > max ? max : default_value);
    return id;
}

int ui_model_add_button(const char *label) {
    return add_widget(UI_WIDGET_BUTTON, label);
}

int ui_model_count(void) {
    return s_count;
}

const ui_widget_t *ui_model_get(int widget_id) {
    if (widget_id < 0 || widget_id >= s_count) {
        return NULL;
    }
    return &s_widgets[widget_id];
}
