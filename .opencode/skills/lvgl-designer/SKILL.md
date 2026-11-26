---
name: lvgl-designer
description: Design and review LVGL 8.4 screens for the 320x240 landscape TFT in dalangs_TFT. Use when creating layouts, choosing colors, spacing, typography, touch targets, or native UI chrome around wasm applets.
---

# LVGL Designer

## Canvas

- Logical resolution is **320x240 native** (this module is 320x240, no rotation).
- The panel scan uses **MX mirror**; touch X mirrors to match.
- The camera preview is a full-screen background image.
- Native chrome is owned by C, never by wasm:
  - no top status bar: BENCH already reports the same metrics
  - bottom tab bar: 24 px icon-only tabs using `LV_SYMBOL_*`, 16 px above the lower edge (resistive dead zone)
  - wasm widget panel: 132 px, rounded, scrollable with a 10 px finger-size scrollbar, hidden for canvas applets
  - HIDE/SHOW sits under the top edge and is the shortest path back to a full-screen preview
  - a 64x18 CPU badge in the top right corner, refreshed once a second
- Canvas applets: LIVE and DRAW. BENCH is a native metrics page.

## Tokens

| Token | Value |
| --- | --- |
| Background | `#05080F` |
| Surface | `#101827` at 88% opacity |
| Text primary | `#FFFFFF` |
| Text muted | `#CBD5E1` |
| Font | Roboto 14/16 and Medium 16 (`main/src/fonts`), Montserrat only for symbols |
| Accent | `#38BDF8` |
| Success | `#22C55E` |
| Danger | `#EF4444` |
| Radius | 8 px surface, 6 px controls |
| Gap | 4 px rows, 8 px sections |

Use `lv_color_hex()` and `LV_OPA_*`; do not invent a second palette per applet.

## Interaction

- Minimum touch target: 32x32 px. Prefer 38 px height for buttons.
- Sliders need a visible value label next to the track.
- Selected tab must be visually distinct (accent background, dark text).
- A button applet marks the mode it is in with `LV_STATE_CHECKED` (accent fill,
  dark label); exactly one button carries it at a time.
- Every control must be reachable with one thumb; no double-tap or long-press required.
- Never block the LVGL task for more than a few milliseconds. Heavy work belongs on another task.
- Always provide a way to hide the settings panel so the camera preview is not
  blocked; the HIDE/SHOW button under the top edge is the current pattern. An
  opaque surface covers the camera outside LIVE and DRAW so the theme stays dark.
- BENCH shows used/total SRAM, used/total PSRAM, core temperature, last wasm load time, and uptime.

## Applet contract

Wasm applets declare widgets through `ui_slider()` and `ui_button()`. They do not create LVGL objects and do not receive pointers. The host renders, styles, and frees them.

Canvas applets draw with `canvas_*` imports into an RGB565 layer. Clearing to
`0x07E0` means "copy the live camera frame here"; there is no chroma key left.

## Review checklist

- Text fits at 320 px wide; no clipped labels.
- Contrast is readable over a bright camera frame.
- Widget panel does not cover the status bar or tab bar.
- Rotated touch coordinates match the visual layout.
- No object is created per frame in a tick callback.
