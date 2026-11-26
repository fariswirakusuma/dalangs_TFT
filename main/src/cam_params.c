#include "cam_params.h"

void cam_params_reset(cam_params_t *params) {
    if (params == NULL) {
        return;
    }
    params->brightness = 0;
    params->contrast = 0;
    params->saturation = 0;
    params->effect = CAM_EFFECT_NONE;
}

int cam_params_clamp(int param_id, int value) {
    switch (param_id) {
    case CAM_PARAM_BRIGHTNESS:
    case CAM_PARAM_CONTRAST:
    case CAM_PARAM_SATURATION:
        if (value < CAM_TUNING_MIN) {
            return CAM_TUNING_MIN;
        }
        if (value > CAM_TUNING_MAX) {
            return CAM_TUNING_MAX;
        }
        return value;

    case CAM_PARAM_EFFECT:
        /* sensor_t accepts 0..6; the UI only uses a subset. */
        if (value < 0) {
            return 0;
        }
        if (value > CAM_EFFECT_SEPIA) {
            return CAM_EFFECT_SEPIA;
        }
        return value;

    default:
        return value;
    }
}

bool cam_params_set(cam_params_t *params, int param_id, int value) {
    if (params == NULL) {
        return false;
    }

    value = cam_params_clamp(param_id, value);

    switch (param_id) {
    case CAM_PARAM_BRIGHTNESS:
        params->brightness = (int8_t)value;
        return true;
    case CAM_PARAM_CONTRAST:
        params->contrast = (int8_t)value;
        return true;
    case CAM_PARAM_SATURATION:
        params->saturation = (int8_t)value;
        return true;
    case CAM_PARAM_EFFECT:
        params->effect = (uint8_t)value;
        return true;
    default:
        return false;
    }
}
