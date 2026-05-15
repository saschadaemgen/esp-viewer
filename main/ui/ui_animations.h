/*
 * ui_animations.h - KOENIGSLIGA Animation-Helper fuer LVGL
 *
 * ESP-Saison 2 Tag 3+
 *
 * Bietet die drei Web-Viewer-Animationen als LVGL-Helper:
 *
 *   ui_anim_breathe    breathing dot im Stream-Hint
 *                      2400ms ease-in-out, opacity 0.45 <-> 1.0
 *                      und scale 1.0 <-> 1.25
 *
 *   ui_anim_bell_pulse expanding pulse-Ring im Ringing-Screen
 *                      2400ms ease-out, scale 0.6 -> 2.2, fade out
 *                      Drei Instanzen mit Delays 0/800/1600ms
 *
 *   ui_anim_bell_wobble Glocken-Wackeln im Ringing-Screen
 *                       2400ms ease-in-out, rotate -10 -> 10 -> 0
 *                       Hauptsaechlich Rotation, kurzer Burst alle 2.4s
 *
 * HARDWARE-NUTZUNG (ESP32-P4):
 *   LVGL 9.2 nutzt PPA (Pixel Processing Accelerator) automatisch
 *   ueber den Espressif-LVGL-Port. Scale + Rotation laufen
 *   hardware-beschleunigt, 60fps mit minimalem CPU-Load.
 *   Alpha-Blending: hardware via DMA2D.
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
 * Startet die "expanding pulse ring"-Animation auf einem Objekt.
 *
 * Animiert kontinuierlich Scale (153 <-> 563, also 0.6x bis 2.2x)
 * und Opacity (230 -> 0). Loop forever mit Delay zwischen den
 * Iterationen.
 *
 * Drei Aufrufe mit delay_ms 0, 800, 1600 erzeugen die drei
 * versetzten Ringe wie im Web-Viewer.
 *
 * @param obj       LVGL-Objekt (Kreis ohne Fill, nur Border)
 * @param delay_ms  Initial-Delay bevor die Animation startet (0/800/1600)
 */
void ui_anim_bell_pulse(lv_obj_t *obj, uint32_t delay_ms);

/**
 * Startet die "bell wobble"-Animation auf einem Objekt.
 *
 * Animiert Rotation in einem Pattern wie im CSS:
 *   0-55%:   0deg
 *   60%:    -10deg
 *   65%:    +10deg
 *   70%:     -6deg
 *   75%:     +6deg
 *   80-100%: 0deg
 *
 * Loop forever, 2400ms pro Cycle.
 *
 * Damit Rotation funktioniert: das Objekt braucht lv_obj_set_style_transform_pivot
 * im Zentrum.
 *
 * @param obj  LVGL-Objekt (typisch die Bell-SVG im Hero)
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
