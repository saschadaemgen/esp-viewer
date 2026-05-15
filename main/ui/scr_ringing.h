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

#ifdef __cplusplus
}
#endif
