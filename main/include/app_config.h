#pragma once

#include "pins.h"

#define APP_NAME "dalangs_TFT"
#define APP_VERSION "0.1.0"

/* Applet order is also the tab order. */
typedef enum {
    APPLET_SLIDERS = 0,
    APPLET_FX,
    APPLET_LIVE_CAM,
    APPLET_DRAW_PAD,
    APPLET_BENCH,
    APPLET_COUNT
} applet_id_t;

#define UI_TAB_BAR_H 24
#define UI_TAB_BAR_MARGIN_BOTTOM 16
#define UI_PANEL_H 132
#define UI_PANEL_MARGIN 4
#define UI_PANEL_Y                                                                                 \
    (UI_V_RES - UI_TAB_BAR_MARGIN_BOTTOM - UI_TAB_BAR_H - UI_PANEL_H - UI_PANEL_MARGIN)
#define UI_CONTENT_H (UI_PANEL_H - 12)
