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

typedef enum {
    SCR_IDLE_MODE_STREAM = 0,    /* Live-Kamera */
    SCR_IDLE_MODE_SCREENSAVER,   /* Uhr/Datum/Wetter */
} scr_idle_mode_t;

typedef struct {
    lv_obj_t *clock_time;
    lv_obj_t *clock_date;
    lv_obj_t *unit_label;
    lv_obj_t *door_meta_label;
    lv_obj_t *modes_container;
    lv_obj_t *stream_view;
    lv_obj_t *screensaver_view;
    lv_obj_t *settings_view;
    lv_obj_t *settings_btn;        /* the third ctrl-group icon */
    bool      settings_shown;
    scr_idle_mode_t current_mode;  /* Stream or Screensaver - settings is overlay */
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
    s_refs.unit_label = unit;

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
    lv_obj_set_style_pad_all(grp, UI_SPACE_2, 0);
    lv_obj_set_style_pad_column(grp, UI_SPACE_2, 0);

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
    lv_obj_t *settings_btn = build_ctrl_button(grp, ICON_SETTINGS, false);
    lv_obj_add_flag(settings_btn, LV_OBJ_FLAG_CLICKABLE);
    s_refs.settings_btn = settings_btn;

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
    s_refs.clock_date = date;

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


/* ---------- modes-container (1fr between topbar and actions) ---------- */
static lv_obj_t *build_modes_container(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_width(container, lv_pct(100));
    lv_obj_set_flex_grow(container, 1);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    /* Children are absolute-positioned (no flex). */
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    /* Clip children to the rounded slot region, so the slide-animation
     * doesn't paint pixels outside the visible area. */
    lv_obj_set_style_radius(container, UI_RADIUS_2XL, 0);
    lv_obj_set_style_clip_corner(container, true, 0);
    return container;
}


/* ---------- .stream slot ---------- */
/* Click on stream/screensaver-view toggles to the other mode.
 * Persistent idle_view_mode in settings remains unchanged. */
static void on_stream_click_toggle(lv_event_t *e)
{
    (void)e;
    scr_idle_toggle_idle_mode();
}

static lv_obj_t *build_stream(lv_obj_t *parent, const scr_idle_data_t *data)
{
    lv_obj_t *stream = lv_obj_create(parent);
    lv_obj_remove_style_all(stream);
    /* Absolute-positioned inside modes_container: full size, top-left anchored.
     * This allows scr_idle_show_settings() to slide stream up via lv_obj_set_y(). */
    lv_obj_set_size(stream, lv_pct(100), lv_pct(100));
    lv_obj_align(stream, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(stream, UI_RADIUS_2XL, 0);
    lv_obj_set_style_clip_corner(stream, true, 0);
    lv_obj_set_style_bg_color(stream, UI_COLOR_STREAM_BG, 0);
    lv_obj_set_style_bg_opa(stream, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(stream, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(stream, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(stream, 1, 0);
    lv_obj_set_style_pad_all(stream, 0, 0);
    lv_obj_clear_flag(stream, LV_OBJ_FLAG_SCROLLABLE);

    /* Tap to switch mode (stream <-> screensaver). */
    lv_obj_add_flag(stream, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stream, on_stream_click_toggle, LV_EVENT_CLICKED, NULL);

    s_refs.stream_view = stream;

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
    s_refs.door_meta_label = meta_lbl;

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
    lv_obj_t *modes_container = build_modes_container(root);
    s_refs.modes_container = modes_container;
    lv_obj_t *stream = build_stream(modes_container, data);
    build_actions(root, data);

    /* Default-Mode beim Boot: Stream sichtbar, Screensaver wird in
     * register_screensaver_view() versteckt. */
    s_refs.current_mode = SCR_IDLE_MODE_STREAM;

    return stream;
}


void scr_idle_set_time(lv_obj_t *screen, const char *now)
{
    (void)screen;
    if (s_refs.clock_time && now) {
        lv_label_set_text(s_refs.clock_time, now);
    }
}

void scr_idle_set_clock_time(const char *txt)
{
    if (s_refs.clock_time && txt) {
        lv_label_set_text(s_refs.clock_time, txt);
    }
}

void scr_idle_set_clock_date(const char *txt)
{
    if (s_refs.clock_date && txt) {
        lv_label_set_text(s_refs.clock_date, txt);
    }
}

void scr_idle_set_unit_name(const char *unit_name)
{
    if (s_refs.unit_label && unit_name) {
        lv_label_set_text(s_refs.unit_label, unit_name);
    }
}

void scr_idle_set_door_name(const char *door_name)
{
    if (s_refs.door_meta_label && door_name) {
        char meta_text[64];
        snprintf(meta_text, sizeof(meta_text), "cam  %s", door_name);
        lv_label_set_text(s_refs.door_meta_label, meta_text);
    }
}


/* ============================================================
 * Modes-Container API: slide animation between stream and settings
 * ============================================================ */

lv_obj_t *scr_idle_get_modes_container(void)
{
    return s_refs.modes_container;
}

void scr_idle_register_settings_view(lv_obj_t *settings_view)
{
    if (!settings_view) return;
    s_refs.settings_view = settings_view;

    /* Hide explicitly - we DO NOT rely on parking outside the visible
     * area, because the modes-container does not clip overflow today.
     * The view will be un-hidden in scr_idle_show_settings(). */
    lv_obj_set_y(settings_view, 0);
    lv_obj_add_flag(settings_view, LV_OBJ_FLAG_HIDDEN);
    s_refs.settings_shown = false;
}

void scr_idle_register_screensaver_view(lv_obj_t *screensaver_view)
{
    if (!screensaver_view) return;
    s_refs.screensaver_view = screensaver_view;

    /* Tap on screensaver toggles back to stream. */
    lv_obj_add_flag(screensaver_view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screensaver_view, on_stream_click_toggle, LV_EVENT_CLICKED, NULL);

    /* Screensaver is hidden by default; main.c will switch to it via
     * scr_idle_show_screensaver() based on idle_view_mode config. */
    lv_obj_set_y(screensaver_view, 0);
    lv_obj_add_flag(screensaver_view, LV_OBJ_FLAG_HIDDEN);
}

void scr_idle_show_stream_mode(void)
{
    if (s_refs.current_mode == SCR_IDLE_MODE_STREAM) return;

    if (s_refs.screensaver_view) {
        lv_obj_add_flag(s_refs.screensaver_view, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_refs.stream_view) {
        lv_obj_clear_flag(s_refs.stream_view, LV_OBJ_FLAG_HIDDEN);
    }
    s_refs.current_mode = SCR_IDLE_MODE_STREAM;
}

void scr_idle_show_screensaver_mode(void)
{
    if (s_refs.current_mode == SCR_IDLE_MODE_SCREENSAVER) return;
    if (!s_refs.screensaver_view) return;   /* not built yet */

    if (s_refs.stream_view) {
        lv_obj_add_flag(s_refs.stream_view, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_refs.screensaver_view, LV_OBJ_FLAG_HIDDEN);
    s_refs.current_mode = SCR_IDLE_MODE_SCREENSAVER;
}

void scr_idle_toggle_idle_mode(void)
{
    if (s_refs.current_mode == SCR_IDLE_MODE_STREAM) {
        scr_idle_show_screensaver_mode();
    } else {
        scr_idle_show_stream_mode();
    }
}

/* Animation exec callback: moves an obj to the y value */
static void anim_set_y_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void start_slide(lv_obj_t *target, int from_y, int to_y)
{
    if (!target) return;
    lv_obj_set_y(target, from_y);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, target);
    lv_anim_set_values(&a, from_y, to_y);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_exec_cb(&a, anim_set_y_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

/* Animation completion callback: when sliding off, also hide it
 * so it does not poke through underneath the topbar or actions-bar. */
static void slide_done_hide_cb(lv_anim_t *a)
{
    lv_obj_t *target = (lv_obj_t *)lv_anim_get_user_data(a);
    if (target) {
        lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
    }
}

void scr_idle_show_settings(void)
{
    if (!s_refs.settings_view || !s_refs.stream_view) return;
    if (s_refs.settings_shown) return;

    lv_coord_t h = lv_obj_get_height(s_refs.modes_container);
    if (h <= 0) h = 800;

    /* Ensure visible before animation. The view was hidden either by
     * register_settings_view (boot) or by the previous show_stream(). */
    lv_obj_clear_flag(s_refs.settings_view, LV_OBJ_FLAG_HIDDEN);

    /* Stream stays put; only the settings view slides up over it. */
    start_slide(s_refs.settings_view, h, 0);

    s_refs.settings_shown = true;
}

void scr_idle_show_stream(void)
{
    if (!s_refs.settings_view || !s_refs.stream_view) return;
    if (!s_refs.settings_shown) return;

    lv_coord_t h = lv_obj_get_height(s_refs.modes_container);
    if (h <= 0) h = 800;

    /* Settings slides back down off-screen, stream stays put underneath.
     * Animation completes, then HIDDEN flag stops residual rendering. */
    lv_anim_delete(s_refs.settings_view, anim_set_y_cb);
    lv_obj_set_y(s_refs.settings_view, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_refs.settings_view);
    lv_anim_set_values(&a, 0, h);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a, anim_set_y_cb);
    lv_anim_set_user_data(&a, s_refs.settings_view);
    lv_anim_set_completed_cb(&a, slide_done_hide_cb);
    lv_anim_start(&a);

    s_refs.settings_shown = false;
}

bool scr_idle_is_settings_shown(void)
{
    return s_refs.settings_shown;
}

void scr_idle_toggle_settings(void)
{
    if (s_refs.settings_shown) {
        scr_idle_show_stream();
    } else {
        scr_idle_show_settings();
    }
}

void scr_idle_set_settings_handler(lv_event_cb_t cb, void *user_data)
{
    if (!s_refs.settings_btn || !cb) return;
    lv_obj_add_event_cb(s_refs.settings_btn, cb, LV_EVENT_CLICKED, user_data);
}
