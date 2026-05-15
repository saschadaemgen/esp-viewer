/*
 * scr_idle.c - Idle screen, direct translation from intercom-idle.html
 *
 * v3: actions use the Lucide icon font (22px, BPP4) shipped as
 *     lucide_22.c/h. All icons (mic, lock-open, history, camera, bell,
 *     settings) render as label glyphs from the Lucide font, matching
 *     the web viewer's icon system 1:1.
 */

#include "scr_idle.h"
#include "ui_tokens.h"
#include "ui_animations.h"
#include "lucide_22.h"

#include "lvgl.h"
#include <stdio.h>

typedef struct {
    lv_obj_t *clock_time;
} scr_idle_refs_t;

static scr_idle_refs_t s_refs;


/* ---------- .identity (left of topbar) ---------- */
static lv_obj_t *build_identity(lv_obj_t *parent, const scr_idle_data_t *data)
{
    lv_obj_t *id = lv_obj_create(parent);
    lv_obj_remove_style_all(id);
    lv_obj_set_size(id, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(id, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(id, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_right(id, UI_SPACE_2, 0);
    lv_obj_set_style_border_color(id, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(id, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(id, 1, 0);
    lv_obj_set_style_border_side(id, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_clear_flag(id, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sub = lv_label_create(id);
    lv_label_set_text(sub, data->dnd ? "NICHT STÖREN" : "ONLINE");
    lv_obj_set_style_text_font(sub, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(sub, UI_OPA_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_letter_space(sub, 1, 0);

    lv_obj_t *unit = lv_label_create(id);
    lv_label_set_text(unit, data->unit_name);
    lv_obj_set_style_text_font(unit, UI_FONT_BASE, 0);
    lv_obj_set_style_text_color(unit, UI_COLOR_TEXT, 0);
    lv_obj_set_style_pad_top(unit, 2, 0);

    return id;
}


/* ---------- .ctrl button inside control-group ---------- */
static lv_obj_t *build_ctrl_button(lv_obj_t *parent, const char *symbol, bool is_on)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, UI_CTRL_BTN_SIZE, UI_CTRL_BTN_SIZE);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lucide_22, 0);
    if (is_on) {
        lv_obj_set_style_text_color(icon, UI_COLOR_DANGER, 0);
    } else {
        lv_obj_set_style_text_color(icon, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_opa(icon, UI_OPA_TEXT_SECONDARY, 0);
    }
    lv_obj_center(icon);
    return btn;
}

static lv_obj_t *build_ctrl_sep(lv_obj_t *parent)
{
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 1, UI_CTRL_SEP_H);
    lv_obj_set_style_bg_color(sep, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(sep, UI_OPA_HAIRLINE, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    return sep;
}


/* ---------- .control-group (center of topbar) ---------- */
static lv_obj_t *build_control_group(lv_obj_t *parent, const scr_idle_data_t *data)
{
    lv_obj_t *grp = lv_obj_create(parent);
    lv_obj_remove_style_all(grp);
    lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(grp, UI_SPACE_1, 0);
    lv_obj_set_style_pad_column(grp, 0, 0);

    lv_obj_set_style_bg_color(grp, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(grp, UI_OPA_SURFACE_1, 0);
    lv_obj_set_style_border_color(grp, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(grp, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(grp, 1, 0);
    lv_obj_set_style_radius(grp, UI_RADIUS_FULL, 0);
    lv_obj_clear_flag(grp, LV_OBJ_FLAG_SCROLLABLE);

    /* Ctrl-buttons use Lucide font glyphs - camera, bell, settings */
    build_ctrl_button(grp, ICON_CAMERA, false);
    build_ctrl_sep(grp);
    build_ctrl_button(grp, ICON_BELL, data->dnd);
    build_ctrl_sep(grp);
    build_ctrl_button(grp, ICON_SETTINGS, false);

    return grp;
}


/* ---------- .clock (right of topbar) ---------- */
static lv_obj_t *build_clock(lv_obj_t *parent, const scr_idle_data_t *data)
{
    lv_obj_t *clk = lv_obj_create(parent);
    lv_obj_remove_style_all(clk);
    lv_obj_set_size(clk, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clk, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clk, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_left(clk, UI_SPACE_2, 0);
    lv_obj_set_style_border_color(clk, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(clk, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(clk, 1, 0);
    lv_obj_set_style_border_side(clk, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_clear_flag(clk, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *date = lv_label_create(clk);
    lv_label_set_text(date, data->now_date);
    lv_obj_set_style_text_font(date, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(date, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(date, UI_OPA_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_letter_space(date, 1, 0);
    lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *time = lv_label_create(clk);
    lv_label_set_text(time, data->now);
    lv_obj_set_style_text_font(time, UI_FONT_BASE, 0);
    lv_obj_set_style_text_color(time, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_top(time, 2, 0);
    s_refs.clock_time = time;

    return clk;
}


/* ---------- .topbar ---------- */
static lv_obj_t *build_topbar(lv_obj_t *parent, const scr_idle_data_t *data)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), UI_TOPBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, UI_SPACE_1, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, UI_SPACE_3, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    build_identity(bar, data);
    build_control_group(bar, data);
    build_clock(bar, data);

    return bar;
}


/* ---------- .stream slot ---------- */
static lv_obj_t *build_stream(lv_obj_t *parent, const scr_idle_data_t *data)
{
    lv_obj_t *stream = lv_obj_create(parent);
    lv_obj_remove_style_all(stream);
    lv_obj_set_width(stream, lv_pct(100));
    lv_obj_set_flex_grow(stream, 1);
    lv_obj_set_style_radius(stream, UI_RADIUS_2XL, 0);
    lv_obj_set_style_clip_corner(stream, true, 0);
    lv_obj_set_style_bg_color(stream, UI_COLOR_STREAM_BG, 0);
    lv_obj_set_style_bg_opa(stream, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(stream, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(stream, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(stream, 1, 0);
    lv_obj_set_style_pad_all(stream, 0, 0);
    lv_obj_clear_flag(stream, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hint = lv_obj_create(stream);
    lv_obj_remove_style_all(hint);
    lv_obj_set_size(hint, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hint, 0, 0);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hint, UI_SPACE_6, 0);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *orb = lv_obj_create(hint);
    lv_obj_remove_style_all(orb);
    lv_obj_set_size(orb, 56, 56);
    lv_obj_set_style_radius(orb, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(orb, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(orb, 10, 0);
    lv_obj_set_style_border_color(orb, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_opa(orb, 15, 0);
    lv_obj_set_style_border_width(orb, 1, 0);
    lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(orb);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(dot, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(dot, 140, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_shadow_color(dot, UI_COLOR_TEXT, 0);
    lv_obj_set_style_shadow_width(dot, 12, 0);
    lv_obj_set_style_shadow_opa(dot, 65, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    ui_anim_breathe(dot);

    lv_obj_t *txt = lv_obj_create(hint);
    lv_obj_remove_style_all(txt);
    lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(txt, 0, 0);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(txt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(txt, 4, 0);
    lv_obj_clear_flag(txt, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl1 = lv_label_create(txt);
    lv_label_set_text(lbl1, "Bereit");
    lv_obj_set_style_text_font(lbl1, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(lbl1, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(lbl1, UI_OPA_TEXT_SECONDARY, 0);

    lv_obj_t *lbl2 = lv_label_create(txt);
    lv_label_set_text(lbl2, "warte auf klingel");
    lv_obj_set_style_text_font(lbl2, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(lbl2, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(lbl2, UI_OPA_TEXT_QUATERNARY, 0);
    lv_obj_set_style_text_letter_space(lbl2, 1, 0);

    lv_obj_t *meta = lv_obj_create(stream);
    lv_obj_remove_style_all(meta);
    lv_obj_set_size(meta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, UI_SPACE_4, -UI_SPACE_4);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta, UI_SPACE_2, 0);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *meta_dot = lv_obj_create(meta);
    lv_obj_remove_style_all(meta_dot);
    lv_obj_set_size(meta_dot, 5, 5);
    lv_obj_set_style_radius(meta_dot, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(meta_dot, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(meta_dot, UI_OPA_TEXT_QUATERNARY, 0);
    lv_obj_clear_flag(meta_dot, LV_OBJ_FLAG_SCROLLABLE);

    char meta_text[64];
    snprintf(meta_text, sizeof(meta_text), "cam  %s", data->door_name);
    lv_obj_t *meta_lbl = lv_label_create(meta);
    lv_label_set_text(meta_lbl, meta_text);
    lv_obj_set_style_text_font(meta_lbl, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(meta_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(meta_lbl, UI_OPA_TEXT_TERTIARY, 0);

    return stream;
}


/* ---------- Action button styles ---------- */

/* Glass-style action button base - returns the button so caller can
 * attach an icon. Optionally renders a primary-blue badge. */
static lv_obj_t *build_action_btn_glass(lv_obj_t *parent, bool show_badge)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, UI_ACTION_BTN_SIZE, UI_ACTION_BTN_SIZE);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, 20, 0);
    lv_obj_set_style_bg_grad_color(btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(btn, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_width(btn, 14, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 6, 0);
    lv_obj_set_style_shadow_opa(btn, UI_SHADOW_MD_OPA, 0);
    lv_obj_set_style_shadow_spread(btn, -6, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    if (show_badge) {
        lv_obj_t *badge = lv_obj_create(btn);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 8, 8);
        lv_obj_set_style_radius(badge, UI_RADIUS_FULL, 0);
        lv_obj_set_style_bg_color(badge, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(badge, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_shadow_width(badge, 6, 0);
        lv_obj_set_style_shadow_opa(badge, UI_OPA_ACCENT_GLOW, 0);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -8, 8);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    }

    return btn;
}


/* Primary-blue action button (Door-open) */
static lv_obj_t *build_action_btn_primary(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, UI_ACTION_BTN_SIZE, UI_ACTION_BTN_SIZE);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT_LIGHT, 0);
    lv_obj_set_style_bg_grad_color(btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_ACCENT_LIGHT, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_color(btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_width(btn, 18, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 8, 0);
    lv_obj_set_style_shadow_opa(btn, UI_OPA_ACCENT_GLOW, 0);
    lv_obj_set_style_shadow_spread(btn, -4, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    return btn;
}


/* ---------- .actions row ---------- */
static lv_obj_t *build_actions(lv_obj_t *parent, const scr_idle_data_t *data)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), UI_ACTIONS_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_top(row, UI_SPACE_2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_ACTION_GAP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Helper to place a Lucide icon (label) in an action button */
    #define PLACE_ICON(btn, icon_code, color) do {                       \
        lv_obj_t *lbl = lv_label_create(btn);                            \
        lv_label_set_text(lbl, icon_code);                               \
        lv_obj_set_style_text_font(lbl, &lucide_22, 0);                     \
        lv_obj_set_style_text_color(lbl, color, 0);                      \
        lv_obj_center(lbl);                                              \
    } while (0)

    /* Mic - glass + filled microphone icon */
    lv_obj_t *btn_mic = build_action_btn_glass(row, false);
    PLACE_ICON(btn_mic, ICON_MIC, UI_COLOR_TEXT);

    /* Tuer auf - primary blue + lock-open icon */
    lv_obj_t *btn_door = build_action_btn_primary(row);
    PLACE_ICON(btn_door, ICON_LOCK_OPEN, UI_COLOR_TEXT_ON_ACCENT);

    /* Verlauf - glass + clock icon (with optional unread badge) */
    lv_obj_t *btn_hist = build_action_btn_glass(row, data->has_unread);
    PLACE_ICON(btn_hist, ICON_HISTORY, UI_COLOR_TEXT);

    #undef PLACE_ICON

    return row;
}


/* ---------- Top-level build ---------- */
lv_obj_t *scr_idle_build(lv_obj_t *screen, const scr_idle_data_t *data)
{
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, UI_SCREEN_PAD, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, UI_SCREEN_GAP, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    build_topbar(root, data);
    lv_obj_t *stream = build_stream(root, data);
    build_actions(root, data);

    return stream;
}


void scr_idle_set_time(lv_obj_t *screen, const char *now)
{
    (void)screen;
    if (s_refs.clock_time && now) {
        lv_label_set_text(s_refs.clock_time, now);
    }
}
