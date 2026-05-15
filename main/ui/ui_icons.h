/*
 * ui_icons.h - Hand-drawn LVGL icons in Apple SF Symbols style
 *
 * Three icons built from primitive lv_obj shapes (rect, line, arc)
 * instead of font glyphs or PNG assets. This keeps the build simple
 * and the icons crisp at any size.
 *
 * Each function creates the icon as a child of `parent` and returns
 * the icon container. The icons are centered inside their container
 * which is sized to `size` x `size` pixels.
 *
 * Color is passed in - white for glass buttons, white for primary
 * blue button.
 *
 * Icons:
 *   ui_icon_mic       - filled microphone (capsule + U-arc + stand)
 *   ui_icon_lock_open - padlock with shackle disengaged right (iOS lock.open)
 *   ui_icon_history   - clock face with hour + minute hand + 12-mark
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a microphone icon centered in `parent`.
 *
 * @param parent  LVGL parent (typically the action button)
 * @param size    Icon container size in pixels (e.g. 22)
 * @param color   Icon color (typically UI_COLOR_TEXT white)
 * @return        Icon container object
 */
lv_obj_t *ui_icon_mic(lv_obj_t *parent, int size, lv_color_t color);

/**
 * Create a lock-open icon centered in `parent`.
 *
 * @param parent  LVGL parent
 * @param size    Icon container size
 * @param color   Icon color
 * @return        Icon container object
 */
lv_obj_t *ui_icon_lock_open(lv_obj_t *parent, int size, lv_color_t color);

/**
 * Create a history (clock) icon centered in `parent`.
 *
 * @param parent  LVGL parent
 * @param size    Icon container size
 * @param color   Icon color
 * @return        Icon container object
 */
lv_obj_t *ui_icon_history(lv_obj_t *parent, int size, lv_color_t color);

#ifdef __cplusplus
}
#endif
