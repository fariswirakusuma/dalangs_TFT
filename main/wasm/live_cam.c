#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

IMPORT(canvas_clear)
extern void canvas_clear(int color565);
IMPORT(canvas_rect)
extern void canvas_rect(int x, int y, int w, int h, int color565);
IMPORT(canvas_flush)
extern void canvas_flush(void);

#define TRANSPARENT 0xF81F
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0

__attribute__((export_name("app_init"))) void app_init(void)
{
    canvas_clear(TRANSPARENT);
    canvas_flush();
}

__attribute__((export_name("app_tick"))) void app_tick(int elapsed_ms)
{
    static int blink;
    blink = (blink + elapsed_ms) % 1000;

    canvas_clear(TRANSPARENT);

    /* Center crosshair. */
    canvas_rect(158, 60, 4, 40, WHITE);
    canvas_rect(140, 78, 40, 4, WHITE);

    /* Recording dot blinks once per second. */
    canvas_rect(300, 28, 10, 10, blink < 500 ? RED : GREEN);
    canvas_rect(0, 20, 320, 2, GREEN);
    canvas_flush();
}
