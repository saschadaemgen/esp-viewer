/*
 * scr_ringing.h - Ringing overlay, 1:1 from library/intercom-ringing.html
 *
 * Layout (.ringing overlay, position absolute inset 0):
 *
 *   Bell hero with 3 expanding pulse rings + wobbling bell
 *   "Klingelt" headline (36px)
 *   DoorName sub (17px)
 *   3 ring-btns: is-danger (Ignorieren) | is-warn (Tuer auf) | is-ok (Annehmen)
 *
 * Template server data:
 *   door_name = "Hauseingang"
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *door_name;
} scr_ringing_data_t;

/**
 * Build the ringing overlay as an absolute-positioned obj
 * covering the entire `parent`.
 *
 * The overlay starts visible (matching intercom-ringing.html
 * default `is-open`). Use lv_obj_add_flag(LV_OBJ_FLAG_HIDDEN)
 * to hide / lv_obj_clear_flag to show.
 *
 * Returns the overlay object so the caller can show/hide it.
 */
lv_obj_t *scr_ringing_build(lv_obj_t *parent, const scr_ringing_data_t *data);

/**
 * Attach a click handler to the reject (red, Ignorieren) button.
 *
 * Must be called AFTER scr_ringing_build. The setter caches the
 * reject button internally on build, so only the most recently
 * built overlay is wired. Calling this multiple times stacks
 * additional callbacks via lv_obj_add_event_cb.
 *
 * @param overlay     The overlay object returned by scr_ringing_build
 *                    (currently unused, reserved for future multi-overlay support)
 * @param cb          LVGL event callback, fired on LV_EVENT_CLICKED
 * @param user_data   Passed through to the callback via lv_event_get_user_data
 */
void scr_ringing_set_reject_handler(lv_obj_t *overlay,
                                    lv_event_cb_t cb,
                                    void *user_data);

/**
 * Attach a click handler to the unlock (orange, Tuer auf) button.
 *
 * Must be called AFTER scr_ringing_build. Same semantics as
 * scr_ringing_set_reject_handler.
 */
void scr_ringing_set_unlock_handler(lv_obj_t *overlay,
                                    lv_event_cb_t cb,
                                    void *user_data);

/**
 * Update the door-name sub-headline shown beneath "Klingelt".
 *
 * Safe to call after scr_ringing_build. NULL is ignored.
 * Used by the /esp/config-Apply pipeline to react to admin
 * changes of location_name without rebuilding the overlay.
 */
void scr_ringing_set_door_name(const char *door_name);

/**
 * Attach a click handler to the accept (green, Annehmen) button.
 *
 * Optional. Bei fehlendem Handler bleibt der Button visuell aktiv
 * aber ohne Wirkung (Two-Way-Audio wird in S5+ aufgesetzt).
 * Same semantics as scr_ringing_set_reject_handler.
 */
void scr_ringing_set_accept_handler(lv_obj_t *overlay,
                                     lv_event_cb_t cb,
                                     void *user_data);

/**
 * Fade-In Animation: macht das Overlay sichtbar via opacity 0->1
 * ueber 400ms ease-out. Bringt das Overlay vor anderen LVGL-Kindern
 * (move_foreground) damit es ueber Stream/Screensaver/Settings liegt.
 *
 * Idempotent - wenn Overlay schon sichtbar wird die Animation neu
 * gestartet (= harmloser visueller flicker).
 */
void scr_ringing_show(void);

/**
 * Fade-Out Animation: opacity 1->0 ueber 400ms ease-out, dann
 * HIDDEN-Flag gesetzt sobald die Animation fertig ist.
 *
 * Idempotent - wenn schon HIDDEN tut nichts.
 */
void scr_ringing_hide(void);

#ifdef __cplusplus
}
#endif
