#pragma once

#include <stdbool.h>

#define UI_MODEL_MAX_WIDGETS 8
#define UI_MODEL_LABEL_LEN 24

typedef enum {
    UI_WIDGET_SLIDER = 0,
    UI_WIDGET_BUTTON,
} ui_widget_type_t;

typedef struct {
    ui_widget_type_t type;
    char label[UI_MODEL_LABEL_LEN];
    int min;
    int max;
    int default_value;
} ui_widget_t;

void ui_model_reset(void);

/* Returns the widget ID, or -1 when the model is full or the label is invalid. */
int ui_model_add_slider(const char *label, int min, int max, int default_value);
int ui_model_add_button(const char *label);

int ui_model_count(void);
const ui_widget_t *ui_model_get(int widget_id);
