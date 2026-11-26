#include "ui_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_add_and_lookup(void) {
    ui_model_reset();
    assert(ui_model_count() == 0);

    int slider = ui_model_add_slider("Brightness", -2, 2, 0);
    int button = ui_model_add_button("Sepia");
    assert(slider == 0);
    assert(button == 1);
    assert(ui_model_count() == 2);

    const ui_widget_t *w = ui_model_get(slider);
    assert(w != NULL);
    assert(w->type == UI_WIDGET_SLIDER);
    assert(strcmp(w->label, "Brightness") == 0);
    assert(w->min == -2 && w->max == 2 && w->default_value == 0);

    w = ui_model_get(button);
    assert(w != NULL);
    assert(w->type == UI_WIDGET_BUTTON);
}

static void test_bounds(void) {
    ui_model_reset();
    assert(ui_model_add_slider("S", 5, -5, 99) == 0);
    const ui_widget_t *w = ui_model_get(0);
    assert(w->min == -5 && w->max == 5 && w->default_value == 5);

    assert(ui_model_get(-1) == NULL);
    assert(ui_model_get(1) == NULL);
}

static void test_limits(void) {
    ui_model_reset();
    assert(ui_model_add_slider(NULL, 0, 1, 0) == -1);
    assert(ui_model_add_button("") == -1);

    char long_label[64];
    memset(long_label, 'A', sizeof(long_label) - 1);
    long_label[sizeof(long_label) - 1] = '\0';

    assert(ui_model_add_slider(long_label, 0, 1, 0) == 0);
    const ui_widget_t *w = ui_model_get(0);
    assert(strlen(w->label) == UI_MODEL_LABEL_LEN - 1);

    for (int i = 1; i < UI_MODEL_MAX_WIDGETS; i++) {
        assert(ui_model_add_button("B") == i);
    }
    assert(ui_model_add_button("overflow") == -1);
}

int main(void) {
    test_add_and_lookup();
    test_bounds();
    test_limits();
    puts("test_ui_model: ok");
    return 0;
}
