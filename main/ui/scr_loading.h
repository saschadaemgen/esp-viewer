/*
 * scr_loading.h - Boot loading screen (own LVGL screen)
 *
 * v2 architecture:
 *   - scr_loading_create() builds its own lv_obj (a SCREEN, parent=NULL)
 *     and returns its handle. The caller activates it with
 *     lv_screen_load(handle) so it becomes the active screen.
 *   - The idle/ringing screens are built on a separate screen handle.
 *   - Switching between loading and idle happens via lv_screen_load_anim
 *     with LV_SCR_LOAD_ANIM_FADE_ON - LVGL handles the transition itself.
 *
 * Why an own screen instead of building on lv_screen_active():
 *   - scr_idle_build calls lv_obj_clean which would destroy loading widgets
 *   - keeping pointers to deleted widgets is a dangling-pointer crash
 *   - separate screens are LVGL's intended pattern for screen transitions
 *
 * Visual (unchanged from v1):
 *   - Pure black background
 *   - Lucide bell icon centered, breathing scale 100-104%
 *   - Soft blue glow behind the bell (LVGL shadow trick, no hard edges)
 *   - Status text below the bell, fades smoothly on update
 *   - Three animated dots
 *   - Spinner
 *   - "unifix v0.1" version label at the bottom
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the loading screen on its own LVGL screen handle.
 * Activates the screen immediately (lv_screen_load).
 *
 * Safe to call only once during boot.
 *
 * @return The screen handle (for reference - typically not used by caller).
 */
lv_obj_t *scr_loading_create(void);

/**
 * Update the status text. The text crossfades smoothly:
 *   200ms fade-out -> text swap -> 200ms fade-in
 *
 * Safe to call only after scr_loading_create.
 * Identical strings are ignored to avoid pointless flicker.
 *
 * @param status  New status string (e.g. "Verbinde WLAN")
 *                Copied internally, caller may free.
 */
void scr_loading_set_status(const char *status);

#ifdef __cplusplus
}
#endif
