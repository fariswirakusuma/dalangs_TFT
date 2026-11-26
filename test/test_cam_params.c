#include "cam_params.h"

#include <assert.h>
#include <stdio.h>

static void test_defaults(void) {
    cam_params_t p;
    p.brightness = 99;
    cam_params_reset(&p);
    assert(p.brightness == 0);
    assert(p.contrast == 0);
    assert(p.saturation == 0);
    assert(p.effect == CAM_EFFECT_NONE);
}

static void test_clamp(void) {
    assert(cam_params_clamp(CAM_PARAM_BRIGHTNESS, -9) == CAM_TUNING_MIN);
    assert(cam_params_clamp(CAM_PARAM_CONTRAST, 9) == CAM_TUNING_MAX);
    assert(cam_params_clamp(CAM_PARAM_SATURATION, 1) == 1);
    assert(cam_params_clamp(CAM_PARAM_EFFECT, -1) == 0);
    assert(cam_params_clamp(CAM_PARAM_EFFECT, 42) == CAM_EFFECT_SEPIA);
}

static void test_set(void) {
    cam_params_t p;
    cam_params_reset(&p);

    assert(cam_params_set(&p, CAM_PARAM_BRIGHTNESS, 1));
    assert(p.brightness == 1);

    assert(cam_params_set(&p, CAM_PARAM_CONTRAST, -7));
    assert(p.contrast == -2);

    assert(cam_params_set(&p, CAM_PARAM_EFFECT, CAM_EFFECT_GRAYSCALE));
    assert(p.effect == 2);

    assert(!cam_params_set(&p, 99, 1));
    assert(!cam_params_set(NULL, CAM_PARAM_BRIGHTNESS, 1));
}

int main(void) {
    test_defaults();
    test_clamp();
    test_set();
    puts("test_cam_params: ok");
    return 0;
}
