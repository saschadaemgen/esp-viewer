/*
 * lucide_22.h - Lucide icon font, 22px, BPP4, uncompressed
 *
 * Generated via lvgl.io/tools/fontconverter from lucide.ttf.
 * Uncompressed to avoid the bpp=4 odd-pixel heap-corruption bug
 * (LVGL issue #1438).
 *
 * S5-17 Konverter-Kompat: der lvgl.io-Konverter fuegt seit ~2025 ein
 * `.static_bitmap = 0,` Feld in den `lv_font_t`-Struct ein (LVGL 9.3+).
 * Unser LVGL 9.2.2 kennt das Feld nicht. Bei JEDEM neuen Font-Export
 * muss die Zeile aus lucide_22.c entfernt werden (siehe Kommentar
 * dort). Alternativ Offline-Konverter `lv_font_conv` verwenden, der
 * das Feld nicht generiert.
 *
 * Contains the following icons used across the CARVILON-Display UI:
 *
 *   Klingel + Idle:
 *     ICON_BELL       0xE059   DND toggle (topbar)
 *     ICON_BELL_OFF   0xE05A   Stumm/Ignorieren (S5-17, Klingel-Toolbar)
 *     ICON_CAMERA     0xE064   Camera toggle (topbar)
 *     ICON_CIRCLE     0xE076   Record (S5-17, korrigiert S5-20)
 *     ICON_LOCK_OPEN  0xE10C   Door-open primary action (idle + klingel)
 *     ICON_MIC        0xE118   Mic action (idle)
 *     ICON_PHONE      0xE133   Accept call (klingel toolbar)
 *     ICON_SETTINGS   0xE154   Settings toggle (topbar)
 *     ICON_X          0xE1B2   Reject call / cancel button
 *     ICON_HISTORY    0xE1F5   History action (idle)
 *     ICON_DOOR_OPEN  0xE3D6   Door-open during ring (alt, ungenutzt)
 *
 *   Settings-Screen:
 *     ICON_INFO       0xE0C0   Help-tooltip trigger
 *     ICON_CHECK      0xE067   Save/confirm button
 *     ICON_SUN_OLD    0xE191   (legacy, replaced by ICON_SUN below)
 *     ICON_MOON       0xE121   Screensaver label
 *     ICON_CLOCK      0xE07F   Alternative screensaver / timer label
 *
 *   Screensaver Wetter-Icons (Master-Chat 19.05.2026 -
 *   korrigiertes 9-Icon-Mapping):
 *     ICON_SUN              0xE25D  Sonne
 *     ICON_CLOUD_SUN        0xE216  heiter (Sonne mit Wolke)
 *     ICON_CLOUD            0xE088  bewoelkt
 *     ICON_CLOUD_FOG        0xE214  Nebel
 *     ICON_CLOUD_DRIZZLE    0xE08A  Nieselregen
 *     ICON_CLOUD_RAIN       0xE08E  Regen
 *     ICON_CLOUD_RAIN_WIND  0xE08F  Schauer
 *     ICON_SNOWFLAKE        0xE165  Schnee
 *     ICON_CLOUD_LIGHTNING  0xE08C  Gewitter
 *
 *   ENTFERNT (im Server-Mapping nicht mehr enthalten):
 *     ICON_CLOUD_SNOW (0xE066) / ICON_CLOUD_HAIL (0xE25C) /
 *     ICON_WIND (0xE1F4)
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

/* Klingel + Idle icons */
#define ICON_BELL       "\xEE\x81\x99"   /* U+E059 */
#define ICON_BELL_OFF   "\xEE\x81\x9A"   /* U+E05A stumm/ignorieren (S5-17) */
#define ICON_CAMERA     "\xEE\x81\xA4"   /* U+E064 */
#define ICON_CIRCLE     "\xEE\x81\xB6"   /* U+E076 circle/record (korrigiert von E069 per lucide.css) */
#define ICON_LOCK_OPEN  "\xEE\x84\x8C"   /* U+E10C */
#define ICON_MIC        "\xEE\x84\x98"   /* U+E118 */
#define ICON_PHONE      "\xEE\x84\xB3"   /* U+E133 */
#define ICON_SETTINGS   "\xEE\x85\x94"   /* U+E154 */
#define ICON_X          "\xEE\x86\xB2"   /* U+E1B2 */
#define ICON_HISTORY    "\xEE\x87\xB5"   /* U+E1F5 */
#define ICON_DOOR_OPEN  "\xEE\x8F\x96"   /* U+E3D6 */

/* Settings-Screen icons */
#define ICON_INFO       "\xEE\x83\x80"   /* U+E0C0 */
#define ICON_CHECK      "\xEE\x81\xA7"   /* U+E067 */
#define ICON_SUN_OLD    "\xEE\x86\x91"   /* U+E191 - legacy */
#define ICON_MOON       "\xEE\x84\xA1"   /* U+E121 */
#define ICON_CLOCK      "\xEE\x81\xBF"   /* U+E07F */

/* Screensaver Wetter-Icons - korrigiertes Master-Chat-Mapping
 * (19.05.2026). Vorherige Codepoints waren teilweise falsch und
 * sind nie aufgefallen weil echtes Wetter im Bildschirmschoner
 * noch nicht getestet war. */
#define ICON_SUN              "\xEE\x89\x9D"   /* U+E25D sonne */
#define ICON_CLOUD_SUN        "\xEE\x88\x96"   /* U+E216 heiter (NEU) */
#define ICON_CLOUD            "\xEE\x82\x88"   /* U+E088 bewoelkt (korrigiert von 0xE08C) */
#define ICON_CLOUD_FOG        "\xEE\x88\x94"   /* U+E214 nebel (korrigiert von 0xE25F) */
#define ICON_CLOUD_DRIZZLE    "\xEE\x82\x8A"   /* U+E08A niesel (korrigiert von 0xE03A) */
#define ICON_CLOUD_RAIN       "\xEE\x82\x8E"   /* U+E08E regen (korrigiert von 0xE26C) */
#define ICON_CLOUD_RAIN_WIND  "\xEE\x82\x8F"   /* U+E08F schauer (NEU) */
#define ICON_SNOWFLAKE        "\xEE\x85\xA5"   /* U+E165 schnee (NEU) */
#define ICON_CLOUD_LIGHTNING  "\xEE\x82\x8C"   /* U+E08C gewitter (korrigiert von 0xE25B) */

#ifdef __cplusplus
}
#endif
