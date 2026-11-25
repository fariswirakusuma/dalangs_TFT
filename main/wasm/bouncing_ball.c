#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

IMPORT(canvas_clear)
extern void canvas_clear(int color565);
IMPORT(canvas_rect)
extern void canvas_rect(int x, int y, int w, int h, int color565);
IMPORT(canvas_flush)
extern void canvas_flush(void);

#define TRANSPARENT 0xF81F
#define BALL 0xF800
#define FLOOR 0x07E0

#define SCREEN_W 320
#define SCREEN_H 240
#define BALL_SIZE 14
#define BOTTOM_Y (SCREEN_H - 24)

/* Position in pixels, velocity in pixels per second. */
static int s_x = 40;
static int s_y = 30;
static int s_vx = 110;
static int s_vy = 90;

__attribute__((export_name("app_init"))) void app_init(void)
{
    s_x = 40;
    s_y = 30;
    s_vx = 110;
    s_vy = 90;
}

__attribute__((export_name("app_tick"))) void app_tick(int elapsed_ms)
{
    if (elapsed_ms <= 0 || elapsed_ms > 200) {
        elapsed_ms = 33;
    }

    s_x += s_vx * elapsed_ms / 1000;
    s_y += s_vy * elapsed_ms / 1000;

    if (s_x <= 0) {
        s_x = 0;
        s_vx = -s_vx;
    } else if (s_x >= SCREEN_W - BALL_SIZE) {
        s_x = SCREEN_W - BALL_SIZE;
        s_vx = -s_vx;
    }

    if (s_y <= 0) {
        s_y = 0;
        s_vy = -s_vy;
    } else if (s_y >= BOTTOM_Y - BALL_SIZE) {
        s_y = BOTTOM_Y - BALL_SIZE;
        s_vy = -s_vy;
    }

    canvas_clear(TRANSPARENT);
    canvas_rect(0, BOTTOM_Y, SCREEN_W, 3, FLOOR);
    canvas_rect(s_x, s_y, BALL_SIZE, BALL_SIZE, BALL);
    canvas_flush();
}
