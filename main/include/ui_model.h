#pragma once

#include <stdbool.h>
#include <stdint.h>

#define UI_MODEL_MAX_WIDGETS 8
#define UI_MODEL_LABEL_LEN 24

typedef enum {
    UI_WIDGET_SLIDER = 0,
    UI_WIDGET_BUTTON,
} ui_widget_type_t;

typedef struct {
    uint8_t type;
    /* Slider bounds are signed: the camera tuning range is -2..2. */
    int8_t min;
    int8_t max;
    int8_t default_value;
    char label[UI_MODEL_LABEL_LEN];
} ui_widget_t;

/* 4 + 24 bytes, no padding: every member is 1 byte wide. */
_Static_assert(sizeof(ui_widget_t) == 28, "ui_widget_t layout changed");

void ui_model_reset(void);

/* Returns the widget ID, or -1 when the model is full or the label is invalid. */
int ui_model_add_slider(const char *label, int min, int max, int default_value);
int ui_model_add_button(const char *label);

int ui_model_count(void);
const ui_widget_t *ui_model_get(int widget_id);
