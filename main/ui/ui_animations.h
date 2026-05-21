/*
 * ui_animations.h - KOENIGSLIGA Animation-Helper fuer LVGL
 *
 * ESP-Saison 2 Tag 3+ (S4-09: bell-pulse weggefallen, wobble auf
 * kontinuierliches Pendel umgestellt).
 *
 * Bietet die Web-Viewer-Animationen als LVGL-Helper:
 *
 *   ui_anim_breathe     breathing dot im Stream-Hint
 *                       2400ms ease-in-out, opacity 0.45 <-> 1.0
 *                       und scale 1.0 <-> 1.25
 *
 *   ui_anim_bell_wobble Pendel-Schwingen der Glocke im Ringing-Screen
 *                       1400ms hin-und-her, +/-10 deg, ease-in-out
 *                       Pivot oben-mittig (Glocke "haengt").
 *
 * HARDWARE-NUTZUNG (ESP32-P4):
 *   Die Stack-Konfig (CONFIG_LV_USE_PPA, CONFIG_LVGL_PORT_ENABLE_PPA)
 *   sind in der aktuellen LVGL 9.2.2 + esp_lvgl_port 2.5.0-Kombination
 *   nicht in der Draw-Pipeline aktiv (siehe S4-07/S4-08 Berichte).
 *   Alle Render-Operationen laufen in Software. Deshalb in S4-09 die
 *   teuren Effekte (Soft-Shadow + Pulse-Scale-Rings) entfernt.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Startet die "breathing dot"-Animation auf einem Objekt.
 *
 * Animiert kontinuierlich Opacity (115 <-> 255) und Scale (256 <-> 320).
 * Loop forever. Wird typisch auf einen kleinen Kreis-Objekt im
 * Stream-Hint angewandt.
 *
 * @param obj  LVGL-Objekt (z.B. lv_obj mit Kreis-Style)
 *
 * Tipp: das Object sollte SIZE bereits gesetzt haben, da LVGL
 * Transform-Scale relativ zur Origin-Size ist.
 */
void ui_anim_breathe(lv_obj_t *obj);

/**
 * Startet die "bell wobble" Pendel-Animation auf einem Objekt.
 *
 * S4-09: kontinuierliches Pendel statt Burst-Pattern. Pivot wird
 * intern auf top-center gesetzt (Aufhaengepunkt der Glocke). Rotation
 * 0 -> +10 -> 0 -> -10 -> 0 in 1400ms, ease-in-out, infinite.
 *
 * @param obj  LVGL-Objekt (typisch der Bell-Hero-Kreis mit der Lucide-Glocke).
 */
void ui_anim_bell_wobble(lv_obj_t *obj);

/**
 * Stoppt alle laufenden Animationen auf einem Objekt.
 *
 * Wird vor Screen-Wechseln aufgerufen damit keine Animationen
 * auf zerstoerte Objekte zugreifen.
 *
 * @param obj  LVGL-Objekt
 */
void ui_anim_stop_all(lv_obj_t *obj);


#ifdef __cplusplus
}
#endif
