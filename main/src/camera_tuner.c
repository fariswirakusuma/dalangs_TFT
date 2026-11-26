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
static bool s_frame_info_logged;

static inline uint8_t clamp_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* YUYV (Y0 U Y1 V) -> RGB565 in LVGL lv_color_t representation. */
static void yuv422_to_rgb565(const uint8_t *src, uint16_t *dst, size_t pixels)
{
    for (size_t i = 0; i + 1 < pixels; i += 2) {
        int y0 = src[0];
        int u = src[1] - 128;
        int y1 = src[2];
        int v = src[3] - 128;
        src += 4;

        int c0 = y0 - 16;
        int c1 = y1 - 16;
        int r = clamp_u8((298 * c0 + 409 * v + 128) >> 8);
        int g = clamp_u8((298 * c0 - 100 * u - 208 * v + 128) >> 8);
        int b = clamp_u8((298 * c0 + 516 * u + 128) >> 8);
        dst[i] = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b).full;

        r = clamp_u8((298 * c1 + 409 * v + 128) >> 8);
        g = clamp_u8((298 * c1 - 100 * u - 208 * v + 128) >> 8);
        b = clamp_u8((298 * c1 + 516 * u + 128) >> 8);
        dst[i + 1] = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b).full;
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
    static int8_t gain = 24;
    int8_t next = gain;
    if (luma < 70 && gain < 30) {
        next = (int8_t)(gain + 1);
    } else if (luma > 110 && gain > 0) {
        next = (int8_t)(gain - 1);
    }
    if (next != gain) {
        gain = next;
        s_sensor->set_agc_gain(s_sensor, gain);
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
#if CONFIG_DALANGS_CAMERA_STATS
            static int64_t s_last_stats_us;
            int64_t now_us = esp_timer_get_time();
            if (now_us - s_last_stats_us > 2000000) {
                s_last_stats_us = now_us;
                uint32_t min_v = 0xFFFF;
                uint32_t max_v = 0;
                uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
                uint32_t colorful = 0;
                uint32_t n = 0;
                uint16_t *px = (uint16_t *)s_frame;
                for (size_t i = 0; i < CAM_FRAME_W * CAM_FRAME_H; i += 97) {
                    uint16_t v = px[i];
                    uint16_t rgb = (uint16_t)((v >> 8) | (v << 8));
                    uint8_t r = (uint8_t)(((rgb >> 11) & 0x1F) << 3);
                    uint8_t g = (uint8_t)(((rgb >> 5) & 0x3F) << 2);
                    uint8_t b = (uint8_t)((rgb & 0x1F) << 3);
                    if (rgb < min_v) min_v = rgb;
                    if (rgb > max_v) max_v = rgb;
                    uint8_t hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
                    uint8_t lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
                    if (hi - lo > 16) colorful++;
                    sum_r += r;
                    sum_g += g;
                    sum_b += b;
                    n++;
                }
                n = n ? n : 1;
                ESP_LOGI(TAG, "frame R=%u G=%u B=%u min=0x%04X max=0x%04X colorful=%u/%u aec=%u gain=%u",
                         (unsigned)(sum_r / n), (unsigned)(sum_g / n), (unsigned)(sum_b / n),
                         (unsigned)min_v, (unsigned)max_v, (unsigned)colorful, (unsigned)n,
                         (unsigned)s_sensor->status.aec_value, (unsigned)s_sensor->status.agc_gain);
            }
#endif
            if (s_frame != NULL && fb->width == CAM_FRAME_W && fb->height == CAM_FRAME_H) {
                if (fb->format == PIXFORMAT_YUV422 && fb->len >= CAM_FRAME_W * CAM_FRAME_H * 2) {
                    uint32_t luma_sum = 0;
                    uint32_t luma_n = 0;
                    for (size_t i = 0; i < CAM_FRAME_W * CAM_FRAME_H; i += 64) {
                        luma_sum += fb->buf[i * 2];
                        luma_n++;
                    }
                    camera_software_ae(luma_sum / (luma_n ? luma_n : 1));

                    yuv422_to_rgb565(fb->buf, (uint16_t *)s_frame, CAM_FRAME_W * CAM_FRAME_H);
                    s_frame_id++;
                } else if (fb->format == PIXFORMAT_RGB565 && fb->len <= CAM_FRAME_BYTES) {
                    memcpy(s_frame, fb->buf, fb->len);
#if CAMERA_BYTE_SWAP
                    uint16_t *pixels = (uint16_t *)s_frame;
                    for (size_t i = 0; i < fb->len / 2; i++) {
                        pixels[i] = (uint16_t)((pixels[i] >> 8) | (pixels[i] << 8));
                    }
#endif
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
    s_sensor->set_awb_gain(s_sensor, 1);
    s_sensor->set_wb_mode(s_sensor, 0);
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
    s_sensor->set_agc_gain(s_sensor, 24);

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
