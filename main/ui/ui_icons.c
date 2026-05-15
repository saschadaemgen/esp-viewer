/*
 * ui_icons.c - Hand-drawn LVGL icon implementations
 *
 * Each icon is built from lv_obj_t primitives (rect with radius,
 * arc, line) parented to a container so the whole icon can be
 * placed and moved as a single unit.
 *
 * Coordinates are computed relative to a 22-pixel base size.
 * Calling with a different `size` scales the elements proportionally.
 *
 * All icons fit inside a centered square of `size` x `size`.
 */

#include "ui_icons.h"
#include <stdlib.h>

/* Helper: scale a base-22 dimension to actual icon size */
static inline int sc(int base_22, int size)
{
    return (base_22 * size) / 22;
}

/* Helper: rect with no scrollbar and clean styling */
static lv_obj_t *icon_rect(lv_obj_t *parent, int w, int h, lv_color_t color,
                            int radius)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_color(r, color, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

/* Helper: centered container holding the icon parts */
static lv_obj_t *icon_container(lv_obj_t *parent, int size)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, size, size);
    lv_obj_center(c);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}


/* ============================================================
 * MICROPHONE ICON (filled)
 *
 * Components (in a 22x22 box):
 *   capsule:  rounded rect 8x12 at (7,2)        - mic body
 *   stand:    arc (U-shape) 14x14 at (4,8)      - lower bracket
 *   post:     vertical rect 2x4 at (10,17)      - drop to base
 *   base:     horizontal rect 8x2 at (7,21)     - foot line
 * ============================================================ */
lv_obj_t *ui_icon_mic(lv_obj_t *parent, int size, lv_color_t color)
{
    lv_obj_t *c = icon_container(parent, size);

    /* Capsule (mic body) */
    lv_obj_t *capsule = icon_rect(c,
        sc(8, size), sc(12, size),
        color, sc(4, size));
    lv_obj_align(capsule, LV_ALIGN_TOP_MID, 0, sc(1, size));

    /* U-stand: built as an arc using lv_arc with thick line */
    lv_obj_t *arc = lv_arc_create(c);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, sc(16, size), sc(16, size));
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, sc(4, size));
    lv_arc_set_bg_angles(arc, 20, 160);
    lv_arc_set_rotation(arc, 0);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, sc(2, size) > 1 ? sc(2, size) : 2, LV_PART_MAIN);
    lv_obj_remove_style(arc, NULL, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    /* Stand post (vertical) */
    lv_obj_t *post = icon_rect(c,
        sc(2, size) > 1 ? sc(2, size) : 2,
        sc(4, size),
        color, sc(1, size));
    lv_obj_align(post, LV_ALIGN_TOP_MID, 0, sc(15, size));

    /* Base foot */
    lv_obj_t *base = icon_rect(c,
        sc(8, size), sc(2, size) > 1 ? sc(2, size) : 2,
        color, sc(1, size));
    lv_obj_align(base, LV_ALIGN_TOP_MID, 0, sc(19, size));

    return c;
}


/* ============================================================
 * LOCK OPEN ICON (variant B: shackle disengaged right)
 *
 * Components (in a 22x22 box):
 *   body:      rounded rect 16x12 at (3,9)        - lock body
 *   shackle:   arc 14x12 at (4,1), curved up      - the U-shape
 *              with right leg ending mid-air (open)
 *   keyhole:   small dot at body center
 * ============================================================ */
lv_obj_t *ui_icon_lock_open(lv_obj_t *parent, int size, lv_color_t color)
{
    lv_obj_t *c = icon_container(parent, size);

    /* Lock body */
    lv_obj_t *body = icon_rect(c,
        sc(16, size), sc(12, size),
        color, sc(2, size));
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, sc(9, size));

    /* Shackle: arc with rotation so the opening is on the right side.
     * LVGL arc angles: 0deg = right (3 o'clock), CCW positive.
     * We want a U-shape: top to left, open right. So we draw from
     * ~180 (left) going up to ~0 (right) but stopping at ~10 to
     * leave the right side open. */
    lv_obj_t *shackle = lv_arc_create(c);
    lv_obj_remove_style_all(shackle);
    int arc_size = sc(14, size);
    lv_obj_set_size(shackle, arc_size, arc_size);
    lv_obj_align(shackle, LV_ALIGN_TOP_MID, 0, sc(2, size));

    /* Set up the arc: from 180 (left) going CCW through 270 (top)
     * and stopping at ~340 (just before right side - that's the
     * disengaged opening). LVGL arc uses degrees with 0 at right,
     * 90 at bottom, 180 at left, 270 at top in default rotation. */
    lv_arc_set_bg_angles(shackle, 180, 350);
    lv_arc_set_rotation(shackle, 180);
    lv_obj_set_style_arc_color(shackle, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(shackle, LV_OPA_COVER, LV_PART_MAIN);
    int stroke = sc(3, size) > 2 ? sc(3, size) : 3;
    lv_obj_set_style_arc_width(shackle, stroke, LV_PART_MAIN);
    lv_obj_remove_style(shackle, NULL, LV_PART_INDICATOR);
    lv_obj_remove_style(shackle, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(shackle, LV_OBJ_FLAG_CLICKABLE);

    return c;
}


/* ============================================================
 * HISTORY (CLOCK) ICON
 *
 * Components (in a 22x22 box):
 *   face:      outline circle 18x18 at (2,2)      - clock face ring
 *              done via 2px-thick lv_obj with radius full
 *   mark12:    tiny dot at top (12 o'clock marker)
 *   hour:      line from center going up (12 position)
 *   minute:    line from center going down-right (4 position)
 *
 * Drawn with stroke style: outline = transparent bg + colored border
 * ============================================================ */
lv_obj_t *ui_icon_history(lv_obj_t *parent, int size, lv_color_t color)
{
    lv_obj_t *c = icon_container(parent, size);

    /* Clock face: ring (border only) */
    int face_size = sc(20, size);
    int stroke = sc(2, size) > 1 ? sc(2, size) : 2;

    lv_obj_t *face = lv_obj_create(c);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, face_size, face_size);
    lv_obj_center(face);
    lv_obj_set_style_bg_opa(face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(face, color, 0);
    lv_obj_set_style_border_opa(face, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(face, stroke, 0);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);

    /* 12-o'clock marker dot above the face */
    int dot_sz = sc(2, size) > 1 ? sc(2, size) : 2;
    lv_obj_t *mark = icon_rect(c, dot_sz, dot_sz, color, dot_sz);
    lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, sc(2, size));

    /* Hour hand: vertical line from center going up (12 position).
     * Length ~5/22, thickness 2px. We use a small rect rotated 0deg. */
    int hand_w = sc(2, size) > 1 ? sc(2, size) : 2;
    lv_obj_t *hour = icon_rect(c, hand_w, sc(6, size), color, sc(1, size));
    lv_obj_align(hour, LV_ALIGN_CENTER, 0, -sc(3, size));

    /* Minute hand: rotated to ~4-o'clock (about 60deg from vertical).
     * In LVGL we use transform_rotate on a vertical rect.
     * Length ~7/22. */
    lv_obj_t *minute = icon_rect(c, hand_w, sc(7, size), color, sc(1, size));
    lv_obj_align(minute, LV_ALIGN_CENTER, 0, sc(2, size));
    /* transform_rotate is in 0.1deg units. Rotate 60deg = 600. */
    lv_obj_set_style_transform_rotation(minute, 600, 0);
    lv_obj_set_style_transform_pivot_x(minute, hand_w / 2, 0);
    lv_obj_set_style_transform_pivot_y(minute, 0, 0);

    return c;
}
