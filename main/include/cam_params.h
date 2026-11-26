#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Camera parameter IDs exposed to Wasm. */
typedef enum {
    CAM_PARAM_BRIGHTNESS = 0,
    CAM_PARAM_CONTRAST = 1,
    CAM_PARAM_SATURATION = 2,
    CAM_PARAM_EFFECT = 3,
} cam_param_id_t;

/* sensor_t special_effect_t values used by the FX applet. */
typedef enum {
    CAM_EFFECT_NONE = 0,
    CAM_EFFECT_NEGATIVE = 1,
    CAM_EFFECT_GRAYSCALE = 2,
    CAM_EFFECT_SEPIA = 6,
} cam_effect_t;

#define CAM_TUNING_MIN (-2)
#define CAM_TUNING_MAX 2

typedef struct {
    /* sensor_t takes -2..2 for these three and 0..6 for the effect. */
    int8_t brightness;
    int8_t contrast;
    int8_t saturation;
    uint8_t effect;
} cam_params_t;

_Static_assert(sizeof(cam_params_t) == 4, "cam_params_t layout changed");

void cam_params_reset(cam_params_t *params);

/* Clamp a value for a known parameter. Returns the clamped value. */
int cam_params_clamp(int param_id, int value);

/* Applies one parameter with clamping. Returns false for an unknown ID. */
bool cam_params_set(cam_params_t *params, int param_id, int value);
