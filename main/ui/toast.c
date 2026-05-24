/*
 * toast.c - Toast-Implementation
 */

#include "toast.h"
#include "ui_tokens.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 * Module state
 * ============================================================ */

#define TOAST_VISIBLE_MS    1500
#define TOAST_FADE_MS        250

typedef struct {
    lv_obj_t    *container;
    lv_obj_t    *label;
    lv_timer_t  *hide_timer;
} toast_state_t;

static toast_state_t s = {0};

/* ============================================================
 * Helpers
 * ============================================================ */

static void fade_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fade_done_cb(lv_anim_t *a)
{
    lv_obj_t *target = (lv_obj_t *)lv_anim_get_user_data(a);
    if (target) {
        lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
    }
}

static void start_fade_out(void)
{
    if (!s.container) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s.container);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, TOAST_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, fade_anim_cb);
    lv_anim_set_user_data(&a, s.container);
    lv_anim_set_completed_cb(&a, fade_done_cb);
    lv_anim_start(&a);
}

static void hide_timer_cb(lv_timer_t *t)
{
    (void)t;
    s.hide_timer = NULL;
    start_fade_out();
}

/* ============================================================
 * Public API
 * ============================================================ */

void toast_init(lv_obj_t *parent)
{
    if (!parent) return;
    if (s.container) return;   /* already inited */

    /* Container: pill, sits at bottom-center */
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_radius(c, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(c, UI_COLOR_BG_ELEV_2, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(c, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_hor(c, 24, 0);
    lv_obj_set_style_pad_ver(c, 12, 0);
    lv_obj_set_style_shadow_color(c, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(c, 24, 0);
    lv_obj_set_style_shadow_ofs_y(c, 4, 0);
    lv_obj_set_style_shadow_opa(c, LV_OPA_30, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    s.container = c;

    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, UI_FONT_BASE, 0);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, 0);
    lv_obj_center(l);
    s.label = l;
}

void toast_show(const char *message)
{
    if (!s.container || !s.label || !message) return;

    /* Stop any ongoing fade-out animation immediately */
    lv_anim_delete(s.container, fade_anim_cb);
    lv_obj_set_style_opa(s.container, LV_OPA_COVER, 0);

    /* Update text and show */
    lv_label_set_text(s.label, message);
    lv_obj_clear_flag(s.container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s.container);

    /* Reset hide-timer (deletes if existing, creates fresh) */
    if (s.hide_timer) {
        lv_timer_delete(s.hide_timer);
        s.hide_timer = NULL;
    }
    s.hide_timer = lv_timer_create(hide_timer_cb, TOAST_VISIBLE_MS, NULL);
    lv_timer_set_repeat_count(s.hide_timer, 1);
}
