/*
 * ui_animations.c - KOENIGSLIGA Animation-Helper Implementation
 *
 * ESP-Saison 2 Tag 3+
 *
 * Hardware-beschleunigt via PPA + DMA2D auf ESP32-P4.
 * Wir setzen keine PPA-Aufrufe selbst - das macht der Espressif-LVGL-Port
 * automatisch fuer transform_scale und transform_rotate Operations.
 */

#include "ui_animations.h"
#include "ui_tokens.h"

#include "esp_log.h"

/* TAG reserved for future logging - currently unused */


/* ============================================================
 * BREATHING DOT
 * ------------------------------------------------------------
 * Web-CSS-Aequivalent:
 *   0%, 100% { opacity: 0.45; transform: scale(1);    }
 *   50%      { opacity: 1.00; transform: scale(1.25); }
 * ease-in-out, 2400ms, infinite
 *
 * In LVGL: zwei parallele Animationen (opa und scale) auf
 * dem gleichen Objekt. Beide playback (ping-pong) damit es
 * sich automatisch zurueckanimiert.
 * ============================================================ */

static void anim_opa_set(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void anim_scale_set(void *obj, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)obj, (uint16_t)value, 0);
}

void ui_anim_breathe(lv_obj_t *obj)
{
    if (!obj) return;

    /* Pivot ins Zentrum setzen, sonst skaliert es von oben links */
    lv_obj_set_style_transform_pivot_x(obj, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(obj, LV_PCT(50), 0);

    /* Opacity-Anim: 115 (~0.45) <-> 255 (1.0) */
    lv_anim_t a_opa;
    lv_anim_init(&a_opa);
    lv_anim_set_var(&a_opa, obj);
    lv_anim_set_values(&a_opa, 115, 255);
    lv_anim_set_duration(&a_opa, UI_DUR_BREATHE / 2);
    lv_anim_set_playback_duration(&a_opa, UI_DUR_BREATHE / 2);
    lv_anim_set_repeat_count(&a_opa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a_opa, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a_opa, anim_opa_set);
    lv_anim_start(&a_opa);

    /* Scale-Anim: 256 (=1.0) <-> 320 (=1.25 in LVGL-Units) */
    lv_anim_t a_scale;
    lv_anim_init(&a_scale);
    lv_anim_set_var(&a_scale, obj);
    lv_anim_set_values(&a_scale, 256, 320);
    lv_anim_set_duration(&a_scale, UI_DUR_BREATHE / 2);
    lv_anim_set_playback_duration(&a_scale, UI_DUR_BREATHE / 2);
    lv_anim_set_repeat_count(&a_scale, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a_scale, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a_scale, anim_scale_set);
    lv_anim_start(&a_scale);
}


/* ============================================================
 * BELL WOBBLE (Pendulum)
 * ------------------------------------------------------------
 * S4-09: vorher CSS-Pattern (kurzer Burst alle 2400ms mit Pause).
 * Sasch will "schwingt sanft hin und her wie eine echte laeutende
 * Glocke" -> kontinuierliches Pendel, kein Burst-mit-Pause.
 *
 * Pattern: 0 -> +10 -> 0 -> -10 -> 0 ueber UI_DUR_BELL_WOBBLE (1400ms).
 * Jede Schwenkphase 350ms (zusammen 700ms hin und 700ms her).
 *
 * Pivot wird in ui_anim_bell_wobble auf TOP-CENTER gesetzt (Glocke
 * haengt oben, schwingt unten - kein "Wackeln um den Bauch").
 *
 * LVGL Rotation-Units: 1/10 Grad. +10 deg = +100 Units.
 * ============================================================ */

/* Lookup-Table fuer Wobble-Pattern.
 * Werte in 1/10 Grad. Time in Promille (0-1000) der Cycle-Length. */
typedef struct {
    int32_t time_promille;
    int32_t rotation_units;
} wobble_kf_t;

static const wobble_kf_t wobble_keyframes[] = {
    {    0,    0 },
    {  250,  100 },   /* +10 deg rechts */
    {  500,    0 },
    {  750, -100 },   /* -10 deg links */
    { 1000,    0 },
};

#define WOBBLE_KF_COUNT (sizeof(wobble_keyframes) / sizeof(wobble_keyframes[0]))

static void anim_wobble_set(void *obj, int32_t value)
{
    /* value ist Time in Promille (0-1000) */
    int32_t t = value;
    int32_t rotation = 0;

    /* Finde die zwei Keyframes zwischen denen wir interpolieren */
    for (size_t i = 0; i < WOBBLE_KF_COUNT - 1; i++) {
        int32_t t0 = wobble_keyframes[i].time_promille;
        int32_t t1 = wobble_keyframes[i + 1].time_promille;
        if (t >= t0 && t <= t1) {
            int32_t r0 = wobble_keyframes[i].rotation_units;
            int32_t r1 = wobble_keyframes[i + 1].rotation_units;
            /* Lineare Interpolation (LVGL macht ease durch path_cb auf t) */
            if (t1 > t0) {
                rotation = r0 + ((r1 - r0) * (t - t0)) / (t1 - t0);
            } else {
                rotation = r0;
            }
            break;
        }
    }
    lv_obj_set_style_transform_rotation((lv_obj_t *)obj, rotation, 0);
}

void ui_anim_bell_wobble(lv_obj_t *obj)
{
    if (!obj) return;

    /* S4-09: Pivot oben-mittig - Glocke haengt oben am Aufhaengepunkt
     * und schwingt unten herum. Mittig (LV_PCT(50)/LV_PCT(50)) sieht
     * aus wie "Wackeln um den Bauch", nicht wie laeutendes Pendel. */
    lv_obj_set_style_transform_pivot_x(obj, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(obj, LV_PCT(0), 0);

    /* Animator 0->1000 in UI_DUR_BELL_WOBBLE (1400ms), loop infinite.
     * Path linear weil das Keyframe-Lookup selbst die Sinus-Form macht. */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, UI_DUR_BELL_WOBBLE);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 0);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, anim_wobble_set);
    lv_anim_start(&a);
}


/* ============================================================
 * STOP ALL
 * ============================================================ */

void ui_anim_stop_all(lv_obj_t *obj)
{
    if (!obj) return;
    lv_anim_delete(obj, NULL);
}
