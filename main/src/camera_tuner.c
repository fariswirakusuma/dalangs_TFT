#include "camera_tuner.h"

#include <stdio.h>
#include <string.h>

#include "cam_params.h"
#include "esp_camera.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pins.h"

static const char *TAG = "CAMERA";

#define CAM_FRAME_BYTES (CAM_FRAME_W * CAM_FRAME_H * 2)

/* Capture at VGA and halve it here: the sensor's own QVGA scaler is noticeably
   softer than a 2x2 box average of a VGA frame. */
#define CAM_SENSOR_W 640
#define CAM_SENSOR_H 480
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

/* The sensor's own white balance never converges and its presets cannot go far
   enough, so the red and blue channels are scaled here until they match green.
   256 is unity gain. */
static uint16_t s_r_gain = 256;
static uint16_t s_b_gain = 256;
static uint32_t s_wb_r, s_wb_g, s_wb_b, s_wb_n;
static char s_sensor_name[24] = "unknown";
static bool s_frame_info_logged;

static inline uint8_t clamp_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* YUYV (Y0 U Y1 V) at VGA -> RGB565 in LVGL lv_color_t representation at QVGA,
   averaging each 2x2 block so the downscale keeps detail instead of aliasing. */
static void yuv422_to_rgb565(const uint8_t *src, uint16_t *dst)
{
    for (size_t oy = 0; oy < CAM_FRAME_H; oy++) {
        const uint8_t *r0 = src + (oy * 2) * CAM_SENSOR_W * 2;
        const uint8_t *r1 = r0 + CAM_SENSOR_W * 2;
        uint16_t *out = dst + oy * CAM_FRAME_W;

        for (size_t ox = 0; ox < CAM_FRAME_W; ox++) {
            size_t bx = ox * 4;
            int y = (r0[bx] + r0[bx + 2] + r1[bx] + r1[bx + 2] + 2) >> 2;
            int u = ((r0[bx + 1] + r1[bx + 1]) >> 1) - 128;
            int v = ((r0[bx + 3] + r1[bx + 3]) >> 1) - 128;

            int c = y - 16;
            uint8_t r = clamp_u8(((298 * c + 409 * v + 128) >> 8) * s_r_gain / 256);
            uint8_t g = clamp_u8((298 * c - 100 * u - 208 * v + 128) >> 8);
            uint8_t b = clamp_u8(((298 * c + 516 * u + 128) >> 8) * s_b_gain / 256);
            s_wb_r += r;
            s_wb_g += g;
            s_wb_b += b;
            s_wb_n++;
            out[ox] = lv_color_make(r, g, b).full;
        }
    }
}

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

/* The OV5640 does not apply its own auto exposure in this build, so the gain is
   driven here from the measured luma. The gain step is coarse, hence +-1.
   shortcut: gain-only, so a very bright scene can still clip; add exposure
   control when the sensor AE is fixed. */
static void camera_software_ae(uint32_t luma)
{
    static int8_t gain = 20;
    static uint16_t aec = 700;

    if (luma < 60) {
        /* Prefer gain in the dark; it reaches further than the exposure line count. */
        if (gain < 30) {
            gain++;
            s_sensor->set_agc_gain(s_sensor, gain);
        } else if (aec < 1100) {
            aec = (uint16_t)(aec + 100);
            s_sensor->set_aec_value(s_sensor, aec);
        }
    } else if (luma > 140) {
        /* Prefer a shorter exposure in bright light; less exposure means less noise. */
        if (aec > 100) {
            aec = (uint16_t)(aec - 100);
            s_sensor->set_aec_value(s_sensor, aec);
        } else if (gain > 0) {
            gain--;
            s_sensor->set_agc_gain(s_sensor, gain);
        }
    }
}

static void camera_task(void *arg)
{
    (void)arg;

    for (;;) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb != NULL) {
            if (!s_frame_info_logged) {
                ESP_LOGI(TAG, "first frame: format=%d %ux%u len=%u", (int)fb->format, fb->width,
                         fb->height, (unsigned)fb->len);
                s_frame_info_logged = true;
            }
            if (s_frame != NULL && fb->width == CAM_SENSOR_W && fb->height == CAM_SENSOR_H) {
                if (fb->format == PIXFORMAT_YUV422 && fb->len >= CAM_SENSOR_W * CAM_SENSOR_H * 2) {
                    uint32_t luma_sum = 0;
                    uint32_t luma_n = 0;
                    for (size_t i = 0; i < CAM_SENSOR_W * CAM_SENSOR_H; i += 256) {
                        luma_sum += fb->buf[i * 2];
                        luma_n++;
                    }
                    camera_software_ae(luma_sum / (luma_n ? luma_n : 1));


                    yuv422_to_rgb565(fb->buf, (uint16_t *)s_frame);

                    if (s_wb_n > 0) {
                        uint32_t ar = s_wb_r / s_wb_n;
                        uint32_t ag = s_wb_g / s_wb_n;
                        uint32_t ab = s_wb_b / s_wb_n;
                        if (ar + 2 < ag && s_r_gain < 420) {
                            s_r_gain = (uint16_t)(s_r_gain + 2);
                        } else if (ar > ag + 2 && s_r_gain > 150) {
                            s_r_gain = (uint16_t)(s_r_gain - 2);
                        }
                        if (ab + 2 < ag && s_b_gain < 420) {
                            s_b_gain = (uint16_t)(s_b_gain + 2);
                        } else if (ab > ag + 2 && s_b_gain > 150) {
                            s_b_gain = (uint16_t)(s_b_gain - 2);
                        }
                    }
                    s_wb_r = s_wb_g = s_wb_b = s_wb_n = 0;

                    s_frame_id++;
                }
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
        .pixel_format = PIXFORMAT_YUV422,
        .frame_size = FRAMESIZE_VGA,
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

    s_sensor->set_framesize(s_sensor, FRAMESIZE_VGA);
    s_sensor->set_vflip(s_sensor, 0);
    s_sensor->set_hmirror(s_sensor, 0);
    /* Auto white balance never converges here, so the daylight gains are fixed and
       the WB mode stays adjustable from the UI. */
    s_sensor->set_whitebal(s_sensor, 1);
    s_sensor->set_awb_gain(s_sensor, 0);
    s_sensor->set_gain_ctrl(s_sensor, 1);
    s_sensor->set_exposure_ctrl(s_sensor, 1);
    s_sensor->set_aec2(s_sensor, 0);
    s_sensor->set_ae_level(s_sensor, 2);
    s_sensor->set_gainceiling(s_sensor, GAINCEILING_128X);
    /* Denoise blurs a 320x240 preview, so trade it for edge sharpening. */
    if (s_sensor->set_denoise) {
        s_sensor->set_denoise(s_sensor, 0);
    }
    if (s_sensor->set_sharpness) {
        s_sensor->set_sharpness(s_sensor, 2);
    }
    s_sensor->set_bpc(s_sensor, 1);
    s_sensor->set_wpc(s_sensor, 1);
    s_sensor->set_lenc(s_sensor, 1);
#if CONFIG_DALANGS_CAMERA_COLORBAR
    s_sensor->set_colorbar(s_sensor, 1);
#else
    s_sensor->set_colorbar(s_sensor, 0);
#endif
    apply_params();

    s_sensor->set_exposure_ctrl(s_sensor, 0);
    s_sensor->set_gain_ctrl(s_sensor, 0);
    s_sensor->set_aec_value(s_sensor, 700);
    s_sensor->set_agc_gain(s_sensor, 20);

    s_ready = true;
    ESP_LOGI(TAG, "%s ready: %dx%d to %dx%d", s_sensor_name, CAM_SENSOR_W, CAM_SENSOR_H,
             CAM_FRAME_W, CAM_FRAME_H);
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
