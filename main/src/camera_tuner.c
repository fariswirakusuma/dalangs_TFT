#include "camera_tuner.h"

#include <stdio.h>
#include <string.h>

#include "cam_params.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pins.h"

static const char *TAG = "CAMERA";

#define CAM_FRAME_BYTES (CAM_FRAME_W * CAM_FRAME_H * 2)
#define CAM_TASK_STACK 4096
#define CAM_TASK_PRIORITY 3
#define CAM_TASK_CORE 1
#define CAM_PERIOD_MS 50

static sensor_t *s_sensor;
static cam_params_t s_params;
static bool s_ready;
static bool s_task_started;
static uint8_t *s_frame;
static uint32_t s_frame_id;
static char s_sensor_name[24] = "unknown";

static void detect_sensor(void)
{
    if (s_sensor == NULL) {
        return;
    }

    uint16_t pid = s_sensor->id.PID;
    switch (pid) {
    case 0x2640:
    case 0x2642:
        snprintf(s_sensor_name, sizeof(s_sensor_name), "OV2640");
        break;
    case 0x3660:
        snprintf(s_sensor_name, sizeof(s_sensor_name), "OV3660");
        break;
    case 0x5640:
        snprintf(s_sensor_name, sizeof(s_sensor_name), "OV5640");
        break;
    case 0x7721:
        snprintf(s_sensor_name, sizeof(s_sensor_name), "OV7725");
        break;
    default:
        snprintf(s_sensor_name, sizeof(s_sensor_name), "PID 0x%04X", pid);
        break;
    }
}

static void apply_params(void)
{
    if (s_sensor == NULL) {
        return;
    }

    s_sensor->set_brightness(s_sensor, (int8_t)s_params.brightness);
    s_sensor->set_contrast(s_sensor, (int8_t)s_params.contrast);
    s_sensor->set_saturation(s_sensor, (int8_t)s_params.saturation);
    s_sensor->set_special_effect(s_sensor, s_params.effect);
}

static void camera_task(void *arg)
{
    (void)arg;

    for (;;) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb != NULL) {
            if (fb->format == PIXFORMAT_RGB565 && fb->width == CAM_FRAME_W && fb->height == CAM_FRAME_H &&
                fb->len <= CAM_FRAME_BYTES && s_frame != NULL) {
                memcpy(s_frame, fb->buf, fb->len);
                s_frame_id++;
            }
            esp_camera_fb_return(fb);
        }
        vTaskDelay(pdMS_TO_TICKS(CAM_PERIOD_MS));
    }
}

esp_err_t camera_tuner_init(void)
{
    s_frame = heap_caps_malloc(CAM_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_frame == NULL) {
        ESP_LOGE(TAG, "no PSRAM for the %d-byte preview buffer", CAM_FRAME_BYTES);
        return ESP_ERR_NO_MEM;
    }
    memset(s_frame, 0, CAM_FRAME_BYTES);

    camera_config_t cfg = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = CAM_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_sensor = esp_camera_sensor_get();
    if (s_sensor == NULL) {
        ESP_LOGE(TAG, "camera sensor handle missing");
        return ESP_FAIL;
    }

    detect_sensor();
    cam_params_reset(&s_params);

    s_sensor->set_framesize(s_sensor, FRAMESIZE_QVGA);
    s_sensor->set_vflip(s_sensor, 0);
    s_sensor->set_hmirror(s_sensor, 0);
    s_sensor->set_whitebal(s_sensor, 1);
    s_sensor->set_gain_ctrl(s_sensor, 1);
    s_sensor->set_exposure_ctrl(s_sensor, 1);
    apply_params();

    s_ready = true;
    ESP_LOGI(TAG, "%s ready at %dx%d RGB565", s_sensor_name, CAM_FRAME_W, CAM_FRAME_H);
    return ESP_OK;
}

void camera_tuner_start(void)
{
    if (!s_ready || s_task_started) {
        return;
    }

    static StaticTask_t task_buffer;
    static StackType_t task_stack[CAM_TASK_STACK];
    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(camera_task, "camera", CAM_TASK_STACK, NULL,
                                                        CAM_TASK_PRIORITY, task_stack, &task_buffer, CAM_TASK_CORE);
    if (handle == NULL) {
        ESP_LOGE(TAG, "camera task creation failed");
        return;
    }
    s_task_started = true;
}

void camera_tuner_set_param(int param_id, int value)
{
    if (!cam_params_set(&s_params, param_id, value)) {
        ESP_LOGW(TAG, "unknown parameter %d", param_id);
        return;
    }
    apply_params();
}

void camera_tuner_reset(void)
{
    cam_params_reset(&s_params);
    apply_params();
}

const uint8_t *camera_tuner_frame_buffer(void)
{
    return s_frame;
}

size_t camera_tuner_frame_size(void)
{
    return CAM_FRAME_BYTES;
}

uint32_t camera_tuner_frame_id(void)
{
    return s_frame_id;
}

bool camera_tuner_ready(void)
{
    return s_ready;
}

const char *camera_tuner_sensor_name(void)
{
    return s_sensor_name;
}
