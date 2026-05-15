/*
 * lucide_22.h - Lucide icon font, 22px, BPP4, uncompressed
 *
 * Generated via lvgl.io/tools/fontconverter from lucide.ttf.
 * Uncompressed to avoid the bpp=4 odd-pixel heap-corruption bug
 * (LVGL issue #1438).
 *
 * Contains nine icons used across the unifix-Display UI:
 *
 *   ICON_BELL       0xE059   DND toggle (topbar)
 *   ICON_CAMERA     0xE064   Camera toggle (topbar)
 *   ICON_LOCK_OPEN  0xE10C   Door-open primary action (idle)
 *   ICON_MIC        0xE118   Mic action (idle)
 *   ICON_PHONE      0xE133   Accept call (ringing overlay)
 *   ICON_SETTINGS   0xE154   Settings toggle (topbar)
 *   ICON_X          0xE1B2   Reject call (ringing overlay)
 *   ICON_HISTORY    0xE1F5   History action (idle)
 *   ICON_DOOR_OPEN  0xE3D6   Door-open during ring (ringing overlay)
 *
 * Usage:
 *   #include "lucide_22.h"
 *   lv_obj_set_style_text_font(label, &lucide_22, 0);
 *   lv_label_set_text(label, ICON_MIC);
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(lucide_22)

/* UTF-8 encoded codepoints */
#define ICON_BELL       "\xEE\x81\x99"   /* U+E059 */
#define ICON_CAMERA     "\xEE\x81\xA4"   /* U+E064 */
#define ICON_LOCK_OPEN  "\xEE\x84\x8C"   /* U+E10C */
#define ICON_MIC        "\xEE\x84\x98"   /* U+E118 */
#define ICON_PHONE      "\xEE\x84\xB3"   /* U+E133 */
#define ICON_SETTINGS   "\xEE\x85\x94"   /* U+E154 */
#define ICON_X          "\xEE\x86\xB2"   /* U+E1B2 */
#define ICON_HISTORY    "\xEE\x87\xB5"   /* U+E1F5 */
#define ICON_DOOR_OPEN  "\xEE\x8F\x96"   /* U+E3D6 */

#ifdef __cplusplus
}
#endif
