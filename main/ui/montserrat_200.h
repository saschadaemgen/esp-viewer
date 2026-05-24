/*
 * montserrat_200.h - Montserrat-Regular 200px subset font
 *
 * Generated via lvgl.io/tools/fontconverter from Montserrat-Regular.ttf.
 * Range: 0x30-0x39 (digits 0-9) and 0x3A (colon).
 * Used exclusively for the screensaver clock display ("17:42").
 *
 * Uncompressed, BPP4. Lives in PSRAM .rodata (XIP).
 *
 * Usage:
 *   #include "montserrat_200.h"
 *   lv_obj_set_style_text_font(clock_label, &montserrat_200, 0);
 *   lv_label_set_text(clock_label, "17:42");
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(montserrat_200)

#ifdef __cplusplus
}
#endif
