/*
 * montserrat_14.h - Montserrat-Regular 14px font with Latin-1 supplement
 *
 * Generated via lvgl.io/tools/fontconverter from Montserrat-Regular.ttf.
 * Replaces LVGL's built-in lv_font_montserrat_14 because the built-in
 * version only contains Basic-ASCII (0x20-0x7F) and is missing umlauts
 * and degree sign.
 *
 * Range:
 *   0x20-0x7F   Basic ASCII (space .. tilde)
 *   0xB0        ° (degree sign)
 *   0xC4, 0xD6, 0xDC, 0xDF, 0xE4, 0xF6, 0xFC   Ä Ö Ü ß ä ö ü
 *
 * Uncompressed, BPP4.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(montserrat_14)

#ifdef __cplusplus
}
#endif
