/*
 * scr_ringing.c - Ringing overlay, direct translation
 *
 * Template structure:
 *
 *   .ringing (overlay)              radial gradient bg, padding 90/24/56
 *     .bell-hero-wrap               160x160 container
 *       .bell-pulse.p1              expanding ring, delay 0
 *       .bell-pulse.p2              expanding ring, delay 800
 *       .bell-pulse.p3              expanding ring, delay 1600
 *       .bell-hero                  solid circle, wobble animation, glow
 *         (bell svg, 80x80, drop-shadow accent-glow)
 *     .ring-text
 *       .ring-headline "Klingelt"   36px semibold
 *       .ring-sub      DoorName     17px regular 65% white
 *     .ring-actions (margin-top auto)
 *       .ring-col
 *         .ring-btn.is-danger       72x72 red, ignore icon
 *         .ring-label "Ignorieren"
 *       .ring-col
 *         .ring-btn.is-warn         72x72 orange, door icon (disabled)
 *         .ring-label "Tür auf"
 *       .ring-col
 *         .ring-btn.is-ok           72x72 green, phone icon (disabled)
 *         .ring-label "Annehmen"
 */

#include "scr_ringing.h"
#include "ui_tokens.h"
#include "ui_animations.h"
#include "lucide_22.h"
#include "lucide_88.h"

#include "lvgl.h"


/*
 * Cached button pointers, set during scr_ringing_build,
 * consumed by scr_ringing_set_*_handler setters.
 *
 * Single-instance: the overlay is built once in main.c on_got_ip.
 * If the overlay is ever rebuilt, the cache simply gets overwritten
 * and the previous overlay's setters become no-ops on the new one.
 */
static lv_obj_t *s_reject_btn = NULL;


/* ---------- Bell hero with pulse rings ---------- */
static void build_bell_hero(lv_obj_t *parent)
{
    /* .bell-hero-wrap: 160x160 relative container */
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, UI_BELL_HERO_SIZE, UI_BELL_HERO_SIZE);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    /* 3 .bell-pulse rings (border-only circles, accent-soft border) */
    for (int i = 0; i < 3; i++) {
        lv_obj_t *ring = lv_obj_create(wrap);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, UI_BELL_HERO_SIZE, UI_BELL_HERO_SIZE);
        lv_obj_center(ring);
        lv_obj_set_style_radius(ring, UI_RADIUS_FULL, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        /* CSS: border 1.5px solid var(--color-accent-soft) */
        lv_obj_set_style_border_color(ring, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_border_opa(ring, UI_OPA_ACCENT_SOFT, 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        ui_anim_bell_pulse(ring, i * 800);
    }

    /* .bell-hero: solid circle with gradient, hairline border, glow,
     * wobble animation */
    lv_obj_t *hero = lv_obj_create(wrap);
    lv_obj_remove_style_all(hero);
    lv_obj_set_size(hero, UI_BELL_HERO_SIZE, UI_BELL_HERO_SIZE);
    lv_obj_center(hero);
    lv_obj_set_style_radius(hero, UI_RADIUS_FULL, 0);
    /* CSS: linear-gradient(180deg, rgba(255,255,255,0.14), rgba(255,255,255,0.03)) */
    lv_obj_set_style_bg_color(hero, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(hero, 35, 0);  /* 0.14 */
    lv_obj_set_style_bg_grad_color(hero, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(hero, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_opa(hero, 25, 0);  /* 0.10 */
    lv_obj_set_style_border_width(hero, 1, 0);
    /* CSS: 0 0 50px accent-glow + 0 0 100px accent-soft */
    lv_obj_set_style_shadow_color(hero, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_width(hero, 80, 0);
    lv_obj_set_style_shadow_opa(hero, UI_OPA_ACCENT_GLOW, 0);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    /* Bell icon - Lucide ICON_BELL at 88px (matches 80x80 SVG in template) */
    lv_obj_t *icon = lv_label_create(hero);
    lv_label_set_text(icon, ICON_BELL);
    lv_obj_set_style_text_font(icon, &lucide_88, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT, 0);
    lv_obj_center(icon);

    ui_anim_bell_wobble(hero);
}


/* ---------- Ring action button ---------- */
typedef enum {
    RING_DANGER,   /* red - Ignorieren */
    RING_WARN,     /* orange - Tuer auf */
    RING_OK,       /* green - Annehmen */
} ring_btn_kind_t;

static lv_obj_t *build_ring_btn(lv_obj_t *parent, const char *symbol,
                                ring_btn_kind_t kind, bool disabled)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, UI_RING_BTN_SIZE, UI_RING_BTN_SIZE);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_color_t bg;
    lv_color_t glow;
    switch (kind) {
    case RING_DANGER:
        bg   = UI_COLOR_DANGER;
        glow = UI_COLOR_DANGER;
        break;
    case RING_WARN:
        bg   = UI_COLOR_WARN;
        glow = UI_COLOR_WARN;
        break;
    case RING_OK:
    default:
        bg   = UI_COLOR_OK;
        glow = UI_COLOR_OK;
        break;
    }

    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    /* CSS shadow: inset 0 1px 0 rgba(255,255,255,0.30), 0 8px 20px <glow> */
    lv_obj_set_style_shadow_color(btn, glow, 0);
    lv_obj_set_style_shadow_width(btn, 20, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 8, 0);
    lv_obj_set_style_shadow_opa(btn, UI_OPA_ACCENT_GLOW, 0);

    /* .ring-btn.is-disabled: opacity 0.42, filter saturate 0.8 */
    if (disabled) {
        lv_obj_set_style_opa(btn, 107, 0); /* 0.42 */
    }

    /* Icon - Lucide font glyph at ~28px (lucide_22 base scaled 1.27x).
     * The scaling is barely visible (within anti-alias tolerance) and
     * avoids generating a second lucide_28 font for these three icons. */
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lucide_22, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT, 0);
    lv_obj_center(icon);
    lv_obj_set_style_transform_scale(icon, 326, 0);  /* 256 = 1.0x, 326 = 1.27x */
    lv_obj_set_style_transform_pivot_x(icon, 11, 0); /* half of 22 */
    lv_obj_set_style_transform_pivot_y(icon, 11, 0);

    return btn;
}


/* ---------- .ring-col (button + label) ----------
 *
 * Returns the button (not the col) so callers can cache it for
 * later event-handler wiring. The col is still constructed; only
 * the return value semantics changed.
 */
static lv_obj_t *build_ring_col(lv_obj_t *parent, const char *symbol,
                                ring_btn_kind_t kind, bool disabled,
                                const char *label_text)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, UI_SPACE_3, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = build_ring_btn(col, symbol, kind, disabled);

    /* .ring-label: 14px medium, rgba(255,255,255,0.85) */
    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, UI_FONT_BASE, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(lbl, 217, 0); /* 0.85 */

    return btn;
}


/* ---------- Top-level build ---------- */
lv_obj_t *scr_ringing_build(lv_obj_t *parent, const scr_ringing_data_t *data)
{
    /* .ringing: absolute inset 0, accent-soft radial gradient bg
     * (simplified as solid black with subtle accent), opacity transition */
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    /* CSS background: layered radial gradients on #000.
     * LVGL has no radial; we use solid black + accent-soft tint via opa. */
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    /* CSS padding: 90px 24px 56px - top large, sides 24, bottom 56 */
    lv_obj_set_style_pad_top(overlay, 90, 0);
    lv_obj_set_style_pad_left(overlay, UI_SPACE_7, 0);
    lv_obj_set_style_pad_right(overlay, UI_SPACE_7, 0);
    lv_obj_set_style_pad_bottom(overlay, 56, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Bell hero */
    build_bell_hero(overlay);

    /* .ring-text: text-align center, margin-top space-9 */
    lv_obj_t *txt = lv_obj_create(overlay);
    lv_obj_remove_style_all(txt);
    lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(txt, 0, 0);
    lv_obj_set_style_margin_top(txt, UI_SPACE_9, 0);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(txt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(txt, UI_SPACE_2, 0);
    lv_obj_clear_flag(txt, LV_OBJ_FLAG_SCROLLABLE);

    /* .ring-headline: 36px semibold, color #fff, letter-spacing tight */
    lv_obj_t *headline = lv_label_create(txt);
    lv_label_set_text(headline, "Klingelt");
    lv_obj_set_style_text_font(headline, UI_FONT_3XL, 0);
    lv_obj_set_style_text_color(headline, UI_COLOR_TEXT, 0);

    /* .ring-sub: 17px regular, rgba(255,255,255,0.65) */
    lv_obj_t *sub = lv_label_create(txt);
    lv_label_set_text(sub, data->door_name);
    lv_obj_set_style_text_font(sub, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(sub, 166, 0); /* 0.65 */

    /* Spacer to push ring-actions to bottom (CSS: margin-top auto) */
    lv_obj_t *spacer = lv_obj_create(overlay);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    /* .ring-actions: flex row, space-between, gap 16, padding 16 */
    lv_obj_t *actions = lv_obj_create(overlay);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_hor(actions, UI_SPACE_5, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(actions, UI_SPACE_5, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    /* 3 columns: Ignorieren / Tür auf (disabled) / Annehmen (disabled).
     * Template marks warn and ok as is-disabled - they activate after answer.
     * The reject button pointer is cached for scr_ringing_set_reject_handler. */
    s_reject_btn = build_ring_col(actions, ICON_X, RING_DANGER, false, "Ignorieren");
    build_ring_col(actions, ICON_DOOR_OPEN, RING_WARN, true, "Tür auf");
    build_ring_col(actions, ICON_PHONE,     RING_OK,   true, "Annehmen");

    return overlay;
}


/* ---------- Handler setters ---------- */
void scr_ringing_set_reject_handler(lv_obj_t *overlay,
                                    lv_event_cb_t cb,
                                    void *user_data)
{
    (void)overlay; /* reserved for future multi-overlay support */
    if (!s_reject_btn || !cb) return;
    lv_obj_add_event_cb(s_reject_btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_add_flag(s_reject_btn, LV_OBJ_FLAG_CLICKABLE);
}
