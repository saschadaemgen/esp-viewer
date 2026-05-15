/*
 * scr_loading.c - Boot loading screen
 *
 * v4: glow is now a pre-rendered ARGB8888 image (glow_blue_400) instead
 *     of an LVGL radial gradient. Pre-rendering avoids the banding
 *     artifacts that LVGL's software gradient renderer produces on
 *     RGB565 displays (known issues #2862, #6696).
 */

#include "scr_loading.h"
#include "ui_tokens.h"
#include "lucide_22.h"
#include "lucide_88.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"

#include <string.h>
#include <stdio.h>


/* ===== State ===== */
static lv_obj_t *s_screen      = NULL;
static lv_obj_t *s_bell        = NULL;
static lv_obj_t *s_status      = NULL;

static char s_pending_status[64] = {0};


/* ===== Animation helpers ===== */

static void anim_set_text_opa(void *obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)obj, v, 0);
}

/* ===== Status text crossfade ===== */
static void status_swap_and_fade_in(lv_anim_t *a)
{
    (void)a;
    if (s_status && s_pending_status[0]) {
        lv_label_set_text(s_status, s_pending_status);
    }

    lv_anim_t in;
    lv_anim_init(&in);
    lv_anim_set_var(&in, s_status);
    lv_anim_set_exec_cb(&in, anim_set_text_opa);
    lv_anim_set_duration(&in, 200);
    lv_anim_set_values(&in, 0, LV_OPA_COVER);
    lv_anim_set_path_cb(&in, lv_anim_path_ease_in);
    lv_anim_start(&in);
}

void scr_loading_set_status(const char *status)
{
    if (!s_status || !status) {
        return;
    }

    if (!bsp_display_lock(50)) {
        return;
    }

    /* Identical text? Skip the crossfade. */
    const char *cur = lv_label_get_text(s_status);
    if (cur && strncmp(cur, status, sizeof(s_pending_status)) == 0) {
        bsp_display_unlock();
        return;
    }

    strncpy(s_pending_status, status, sizeof(s_pending_status) - 1);
    s_pending_status[sizeof(s_pending_status) - 1] = 0;

    lv_anim_t out;
    lv_anim_init(&out);
    lv_anim_set_var(&out, s_status);
    lv_anim_set_exec_cb(&out, anim_set_text_opa);
    lv_anim_set_duration(&out, 200);
    lv_anim_set_values(&out, LV_OPA_COVER, 0);
    lv_anim_set_path_cb(&out, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&out, status_swap_and_fade_in);
    lv_anim_start(&out);

    bsp_display_unlock();
}


/* ===== Build ===== */
lv_obj_t *scr_loading_create(void)
{
    if (s_screen) {
        return s_screen;
    }

    /* Own screen, no parent -> top-level LVGL screen */
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Activate this screen */
    lv_screen_load(s_screen);

    /* No glow for now - bell stands alone on black */

    /* Bell icon: ICON_BELL at 88px */
    s_bell = lv_label_create(s_screen);
    lv_label_set_text(s_bell, ICON_BELL);
    lv_obj_set_style_text_font(s_bell, &lucide_88, 0);
    lv_obj_set_style_text_color(s_bell, UI_COLOR_TEXT, 0);
    lv_obj_align(s_bell, LV_ALIGN_CENTER, 0, -80);

    /* Status text */
    s_status = lv_label_create(s_screen);
    lv_label_set_text(s_status, "Starte");
    lv_obj_set_style_text_font(s_status, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(s_status, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(s_status, LV_OPA_COVER, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, -10);

    /* Spinner under the status text */
    lv_obj_t *spinner = lv_spinner_create(s_screen);
    lv_obj_set_size(spinner, 28, 28);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x1f1f1f), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);

    /* Version label at the bottom */
    lv_obj_t *version = lv_label_create(s_screen);
    lv_label_set_text(version, "unifix v0.1");
    lv_obj_set_style_text_font(version, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(version, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(version, UI_OPA_TEXT_QUATERNARY, 0);
    lv_obj_set_style_text_letter_space(version, 1, 0);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -32);

    /* Kick off the animations */

    return s_screen;
}
