#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

IMPORT(canvas_clear)
extern void canvas_clear(int color565);
IMPORT(canvas_rect)
extern void canvas_rect(int x, int y, int w, int h, int color565);
IMPORT(canvas_line)
extern void canvas_line(int x0, int y0, int x1, int y1, int color565);
IMPORT(canvas_flush)
extern void canvas_flush(void);

#define TRANSPARENT 0x07E0
#define WHITE 0xFFFF
#define CYAN 0x07FF

static int s_last_x = -1;
static int s_last_y = -1;

__attribute__((export_name("app_init"))) void app_init(void) {
    s_last_x = -1;
    s_last_y = -1;
    canvas_clear(TRANSPARENT);
    canvas_flush();
}

__attribute__((export_name("app_touch"))) void app_touch(int x, int y, int pressed) {
    if (!pressed) {
        s_last_x = -1;
        s_last_y = -1;
        return;
    }

    if (s_last_x >= 0 && s_last_y >= 0) {
        canvas_line(s_last_x, s_last_y, x, y, WHITE);
    } else {
        canvas_rect(x - 1, y - 1, 3, 3, CYAN);
    }

    s_last_x = x;
    s_last_y = y;
    canvas_flush();
}
