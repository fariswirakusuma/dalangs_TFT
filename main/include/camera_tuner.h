#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t camera_tuner_init(void);
/* Capture loop. Created by app_tasks_start(); it owns its own pacing. */
void camera_tuner_task(void *arg);

void camera_tuner_set_param(int param_id, int value);
void camera_tuner_reset(void);

/* PSRAM frame buffer in RGB565, or NULL when allocation failed. */
const uint8_t *camera_tuner_frame_buffer(void);
size_t camera_tuner_frame_size(void);

/* Increments after each captured frame; the UI uses it to avoid redundant redraws. */
uint32_t camera_tuner_frame_id(void);

bool camera_tuner_ready(void);
const char *camera_tuner_sensor_name(void);
