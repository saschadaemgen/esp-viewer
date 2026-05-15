/*
 * scr_idle.h - Idle screen, 1:1 from library/intercom-idle.html
 *
 * Layout (matches CSS .screen-idle grid-template-rows: 64px 1fr 96px):
 *
 *   topbar 64px   identity | control-group | clock
 *   stream 1fr    radius 22px, dark, stream canvas embedded here
 *   actions 96px  Mic | Tuer-auf (primary) | Verlauf
 *
 * Template server data:
 *   unit_name  = "Daemgen"
 *   door_name  = "Hauseingang"
 *   now        = "14:23:01"
 *   now_date   = "Di, 13. Mai"
 *   dnd        = false
 *   has_unread = false
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *unit_name;
    const char *door_name;
    const char *now;
    const char *now_date;
    bool dnd;
    bool has_unread;
} scr_idle_data_t;

/**
 * Build idle screen on the given lv_screen.
 *
 * Returns the .stream slot (lv_obj_t*) so the MJPEG pipeline
 * can render its canvas inside.
 */
lv_obj_t *scr_idle_build(lv_obj_t *screen, const scr_idle_data_t *data);

/**
 * Update the clock-time label.
 * Pass the screen returned/used in scr_idle_build.
 */
void scr_idle_set_time(lv_obj_t *screen, const char *now);

#ifdef __cplusplus
}
#endif
