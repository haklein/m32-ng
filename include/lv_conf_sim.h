// Simulator-only LVGL font overrides.
// The simulator bypasses the shared lv_conf.h (-D LV_CONF_SKIP) but still
// needs the Intel One Mono fonts declared and set as default.
//
// No #includes here — this header is force-included (-include) before
// LVGL's own include paths are resolved.  These are pure macro definitions
// that LVGL will expand later when processing its internal headers.

#pragma once

// Tell LVGL about the custom font symbols (extern declarations).
#undef  LV_FONT_CUSTOM_DECLARE
#define LV_FONT_CUSTOM_DECLARE \
    LV_FONT_DECLARE(lv_font_intel_14) \
    LV_FONT_DECLARE(lv_font_intel_20) \
    LV_FONT_DECLARE(lv_font_intel_28)

// Keep Montserrat 14 as the default (it includes icon glyphs needed
// by menus, status bar, etc.).  Intel One Mono is applied explicitly
// on CW text areas via lv_obj_set_style_text_font().
