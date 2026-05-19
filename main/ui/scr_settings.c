/*
 * scr_settings.c - Settings-Screen Implementation (5 Sektionen, Auto-Save)
 *
 * Implementiert Master-Chat-Spec S14-XX 1:1:
 *   - Idle-Ansicht (3 Radios: Bildschirmschoner / Live-Stream / Bildschirm aus)
 *   - Auto-Bildschirmschoner (5 Radios)
 *   - Bildschirm aus nach (6 Radios)
 *   - Helligkeit (Slider, 300ms Debounce)
 *   - Sprache (2 Radios)
 *   - CARVILON-Footer
 *
 * Auto-Save: jede Aenderung loest sofort den change_handler aus
 *   (bei Slider nach 300ms Debounce).
 */

#include "scr_settings.h"
#include "ui_tokens.h"
#include "lucide_22.h"

#include "lvgl.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 * Constants and presets
 * ============================================================ */

/* Auto-Screensaver - Master-Chat-Allow-List */
#define AUTO_SS_COUNT  5

typedef struct {
    int value;
    const char *label;
} preset_t;

static const preset_t s_auto_ss_presets[AUTO_SS_COUNT] = {
    {   0, "aus"        },
    {  30, "30 Sekunden"},
    {  60, "1 Minute"   },
    { 300, "5 Minuten"  },
    { 600, "10 Minuten" },
};

/* Screen-Off - Master-Chat-Allow-List */
#define SCREEN_OFF_COUNT  6

static const preset_t s_screen_off_presets[SCREEN_OFF_COUNT] = {
    {    0, "nie"        },
    {   30, "30 Sekunden"},
    {   60, "1 Minute"   },
    {  300, "5 Minuten"  },
    {  600, "10 Minuten" },
    { 1800, "30 Minuten" },
};

/* Idle-Mode-Radios */
#define IDLE_MODE_COUNT  3

typedef struct {
    idle_view_mode_t mode;
    const char *label;
} idle_mode_preset_t;

static const idle_mode_preset_t s_idle_mode_presets[IDLE_MODE_COUNT] = {
    { IDLE_MODE_SCREENSAVER, "Bildschirmschoner (Uhr & Wetter)" },
    { IDLE_MODE_LIVESTREAM,  "Live-Stream (T\xc3\xbcrkamera)"   },
    { IDLE_MODE_SCREEN_OFF,  "Bildschirm aus"                    },
};

/* Language radios */
#define LANG_COUNT  2

typedef struct {
    language_t value;
    const char *label;
} lang_preset_t;

static const lang_preset_t s_lang_presets[LANG_COUNT] = {
    { LANG_DE, "Deutsch" },
    { LANG_EN, "English" },
};

/* Clock-Layout radios (S03-09) */
#define CLOCK_LAYOUT_COUNT  2

typedef struct {
    clock_layout_t value;
    const char *label;
} clock_layout_preset_t;

static const clock_layout_preset_t s_clock_layout_presets[CLOCK_LAYOUT_COUNT] = {
    { CLOCK_LAYOUT_VERTICAL,   "Vertikal"   },
    { CLOCK_LAYOUT_HORIZONTAL, "Horizontal" },
};

/* Brightness debounce */
#define BRIGHTNESS_DEBOUNCE_MS  300

/* Tooltip-Texte */
#define TOOLTIP_TEXT_IDLE_VIEW                                       \
    "Was wird angezeigt wenn das Display ungenutzt ist?\n"           \
    "Bildschirmschoner zeigt Uhr und Wetter. Live-Stream\n"          \
    "zeigt die T\xc3\xbcrkamera. 'Bildschirm aus' schaltet den\n"    \
    "Bildschirm komplett aus."

#define TOOLTIP_TEXT_AUTO_SS                                         \
    "Nach welcher Zeit ohne Ber\xc3\xbchrung soll der\n"             \
    "Bildschirmschoner aktiviert werden?\n"                          \
    "'aus' deaktiviert die Automatik."

#define TOOLTIP_TEXT_SCREEN_OFF                                      \
    "Nach welcher Zeit ohne Ber\xc3\xbchrung soll der\n"             \
    "Bildschirm komplett ausgeschaltet werden?\n"                    \
    "'nie' h\xc3\xa4lt den Bildschirm immer an. Bei Klingeln\n"      \
    "wird sofort wieder eingeschaltet."

#define TOOLTIP_TEXT_BRIGHTNESS                                      \
    "Stellt die Display-Helligkeit ein.\n"                           \
    "H\xc3\xb6here Werte k\xc3\xb6nnen den Energieverbrauch\n"       \
    "erh\xc3\xb6hen."

#define TOOLTIP_TEXT_LANGUAGE                                        \
    "Sprache der Anzeige (Datum, Wochentag, Beschriftungen)."

#define TOOLTIP_TEXT_CLOCK_LAYOUT                                    \
    "Vertikale Stunden- und Minuten-Anzeige im\n"                    \
    "Android-Pixel-Style oder klassische\n"                          \
    "horizontale HH:MM-Anzeige."

/* ============================================================
 * Module state
 * ============================================================ */

typedef struct {
    /* Top-level */
    lv_obj_t *root;

    /* Header X */
    lv_obj_t *close_btn;

    /* Idle-Mode section */
    lv_obj_t *idle_radios[IDLE_MODE_COUNT];
    lv_obj_t *idle_dots[IDLE_MODE_COUNT];

    /* Auto-Screensaver section - container + radios.
     * Container is hidden unless idle_mode == SCREENSAVER. */
    lv_obj_t *auto_ss_section;
    lv_obj_t *auto_ss_radios[AUTO_SS_COUNT];
    lv_obj_t *auto_ss_dots[AUTO_SS_COUNT];

    /* Screen-Off section - container + radios.
     * Container is hidden unless idle_mode == SCREEN_OFF. */
    lv_obj_t *screen_off_section;
    lv_obj_t *screen_off_radios[SCREEN_OFF_COUNT];
    lv_obj_t *screen_off_dots[SCREEN_OFF_COUNT];

    /* Brightness section */
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value_lbl;
    lv_timer_t *brightness_debounce_timer;

    /* Language section */
    lv_obj_t *lang_radios[LANG_COUNT];
    lv_obj_t *lang_dots[LANG_COUNT];

    /* Clock-Layout section (S03-09) */
    lv_obj_t *clock_layout_radios[CLOCK_LAYOUT_COUNT];
    lv_obj_t *clock_layout_dots[CLOCK_LAYOUT_COUNT];

    /* Tooltip modal */
    lv_obj_t *tooltip_modal;

    /* Pending values - reflect what's shown right now */
    idle_view_mode_t  pending_idle_mode;
    int               pending_auto_ss;
    int               pending_screen_off;
    int               pending_brightness;
    language_t        pending_language;
    clock_layout_t    pending_clock_layout;

    /* Suppress change_handler during external updates */
    bool external_update_in_progress;

    /* Callbacks */
    scr_settings_brightness_preview_cb_t preview_cb;
    void *preview_cb_user_data;

    scr_settings_change_cb_t change_cb;
    void *change_cb_user_data;
} scr_settings_state_t;

static scr_settings_state_t s = {0};

/* ============================================================
 * Forward declarations
 * ============================================================ */

static void notify_change(void);
static void update_idle_radios(void);
static void update_auto_ss_radios(void);
static void update_screen_off_radios(void);
static void update_lang_radios(void);
static void update_clock_layout_radios(void);
static void update_section_visibility(void);

/* ============================================================
 * Change notification
 * ============================================================ */

static void notify_change(void)
{
    if (s.external_update_in_progress) return;
    if (!s.change_cb) return;

    scr_settings_snapshot_t snap = {
        .idle_view_mode       = s.pending_idle_mode,
        .auto_screensaver_sec = s.pending_auto_ss,
        .screen_off_sec       = s.pending_screen_off,
        .brightness           = s.pending_brightness,
        .language             = s.pending_language,
        .clock_layout         = s.pending_clock_layout,
    };
    s.change_cb(&snap, s.change_cb_user_data);
}

/* ============================================================
 * Tooltip modal
 * ============================================================ */

static void tooltip_close_cb(lv_event_t *e)
{
    (void)e;
    if (s.tooltip_modal) {
        lv_obj_add_flag(s.tooltip_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

static void tooltip_show(const char *text)
{
    if (!s.tooltip_modal) return;

    lv_obj_t *card = lv_obj_get_child(s.tooltip_modal, 0);
    if (card) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(card); i++) {
            lv_obj_t *child = lv_obj_get_child(card, i);
            if (child && lv_obj_check_type(child, &lv_label_class)) {
                const char *cur = lv_label_get_text(child);
                if (cur && strlen(cur) > 8) {
                    lv_label_set_text(child, text);
                }
            }
        }
    }

    lv_obj_clear_flag(s.tooltip_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s.tooltip_modal);
}

static void tt_idle_cb(lv_event_t *e)         { (void)e; tooltip_show(TOOLTIP_TEXT_IDLE_VIEW); }
static void tt_auto_ss_cb(lv_event_t *e)      { (void)e; tooltip_show(TOOLTIP_TEXT_AUTO_SS); }
static void tt_screen_off_cb(lv_event_t *e)   { (void)e; tooltip_show(TOOLTIP_TEXT_SCREEN_OFF); }
static void tt_brightness_cb(lv_event_t *e)   { (void)e; tooltip_show(TOOLTIP_TEXT_BRIGHTNESS); }
static void tt_language_cb(lv_event_t *e)     { (void)e; tooltip_show(TOOLTIP_TEXT_LANGUAGE); }
static void tt_clock_layout_cb(lv_event_t *e) { (void)e; tooltip_show(TOOLTIP_TEXT_CLOCK_LAYOUT); }

static lv_obj_t *build_tooltip_modal(lv_obj_t *parent)
{
    lv_obj_t *scrim = lv_obj_create(parent);
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, lv_pct(100), lv_pct(100));
    lv_obj_align(scrim, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(scrim, UI_COLOR_SCRIM, 0);
    lv_obj_set_style_bg_opa(scrim, UI_OPA_SCRIM, 0);
    lv_obj_set_style_border_width(scrim, 0, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scrim, tooltip_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(scrim);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 560, 320);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, UI_COLOR_MODAL_BG, 0);
    lv_obj_set_style_bg_opa(card, UI_OPA_MODAL_BG, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_XL, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(card, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, UI_SPACE_6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, NULL, LV_EVENT_CLICKED, NULL);

    /* Close-X top-right */
    lv_obj_t *close_btn = lv_obj_create(card);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 36, 36);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(close_btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(close_btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(close_btn, UI_OPA_SURFACE_2, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_btn, tooltip_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *x_icon = lv_label_create(close_btn);
    lv_label_set_text(x_icon, ICON_X);
    lv_obj_set_style_text_font(x_icon, &lucide_22, 0);
    lv_obj_set_style_text_color(x_icon, UI_COLOR_TEXT, 0);
    lv_obj_center(x_icon);

    /* Long text label */
    lv_obj_t *txt = lv_label_create(card);
    lv_label_set_text(txt, "                    ");   /* > 8 chars placeholder */
    lv_obj_set_style_text_font(txt, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(txt, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(txt, UI_OPA_TEXT_SECONDARY, 0);
    lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(txt, 480);
    lv_obj_align(txt, LV_ALIGN_CENTER, 0, 16);

    return scrim;
}

/* ============================================================
 * Radio click callbacks
 * ============================================================ */

static void idle_mode_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= IDLE_MODE_COUNT) return;
    if (s.pending_idle_mode == s_idle_mode_presets[idx].mode) return;
    s.pending_idle_mode = s_idle_mode_presets[idx].mode;
    update_idle_radios();
    update_section_visibility();
    notify_change();
}

static void auto_ss_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= AUTO_SS_COUNT) return;
    if (s.pending_auto_ss == s_auto_ss_presets[idx].value) return;
    s.pending_auto_ss = s_auto_ss_presets[idx].value;
    update_auto_ss_radios();
    notify_change();
}

static void screen_off_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= SCREEN_OFF_COUNT) return;
    if (s.pending_screen_off == s_screen_off_presets[idx].value) return;
    s.pending_screen_off = s_screen_off_presets[idx].value;
    update_screen_off_radios();
    notify_change();
}

static void lang_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= LANG_COUNT) return;
    if (s.pending_language == s_lang_presets[idx].value) return;
    s.pending_language = s_lang_presets[idx].value;
    update_lang_radios();
    notify_change();
}

static void clock_layout_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= CLOCK_LAYOUT_COUNT) return;
    if (s.pending_clock_layout == s_clock_layout_presets[idx].value) return;
    s.pending_clock_layout = s_clock_layout_presets[idx].value;
    update_clock_layout_radios();
    notify_change();
}

/* ============================================================
 * Brightness slider (with debounce)
 * ============================================================ */

static void brightness_debounce_cb(lv_timer_t *t)
{
    (void)t;
    s.brightness_debounce_timer = NULL;
    notify_change();
}

static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    int value = lv_slider_get_value(slider);

    s.pending_brightness = value;

    /* Update value label */
    if (s.brightness_value_lbl) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", value);
        lv_label_set_text(s.brightness_value_lbl, buf);
    }

    /* Live preview - immediately, no debounce */
    if (s.preview_cb) {
        s.preview_cb(value, s.preview_cb_user_data);
    }

    if (s.external_update_in_progress) return;

    /* Reset debounce timer */
    if (s.brightness_debounce_timer) {
        lv_timer_delete(s.brightness_debounce_timer);
        s.brightness_debounce_timer = NULL;
    }
    s.brightness_debounce_timer = lv_timer_create(brightness_debounce_cb,
                                                   BRIGHTNESS_DEBOUNCE_MS,
                                                   NULL);
    lv_timer_set_repeat_count(s.brightness_debounce_timer, 1);
}

/* ============================================================
 * Radio visual update helpers
 * ============================================================ */

static void apply_radio_visual(lv_obj_t *outer, lv_obj_t *dot, bool active)
{
    if (dot) {
        lv_obj_set_style_bg_opa(dot, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (outer) {
        lv_obj_set_style_border_color(outer,
                                      active ? UI_COLOR_ACCENT : UI_COLOR_TEXT, 0);
        lv_obj_set_style_border_opa(outer,
                                    active ? LV_OPA_COVER : UI_OPA_HAIRLINE_STRONG, 0);
    }
}

static void update_idle_radios(void)
{
    for (int i = 0; i < IDLE_MODE_COUNT; i++) {
        bool active = (s_idle_mode_presets[i].mode == s.pending_idle_mode);
        apply_radio_visual(s.idle_radios[i], s.idle_dots[i], active);
    }
}

static void update_auto_ss_radios(void)
{
    for (int i = 0; i < AUTO_SS_COUNT; i++) {
        bool active = (s_auto_ss_presets[i].value == s.pending_auto_ss);
        apply_radio_visual(s.auto_ss_radios[i], s.auto_ss_dots[i], active);
    }
}

static void update_screen_off_radios(void)
{
    for (int i = 0; i < SCREEN_OFF_COUNT; i++) {
        bool active = (s_screen_off_presets[i].value == s.pending_screen_off);
        apply_radio_visual(s.screen_off_radios[i], s.screen_off_dots[i], active);
    }
}

static void update_lang_radios(void)
{
    for (int i = 0; i < LANG_COUNT; i++) {
        bool active = (s_lang_presets[i].value == s.pending_language);
        apply_radio_visual(s.lang_radios[i], s.lang_dots[i], active);
    }
}

static void update_clock_layout_radios(void)
{
    for (int i = 0; i < CLOCK_LAYOUT_COUNT; i++) {
        bool active = (s_clock_layout_presets[i].value == s.pending_clock_layout);
        apply_radio_visual(s.clock_layout_radios[i], s.clock_layout_dots[i], active);
    }
}

/* ============================================================
 * Context-dependent section visibility
 *
 * "Auto-Bildschirmschoner"  visible only if idle_mode == SCREENSAVER
 * "Bildschirm aus nach"     visible only if idle_mode == SCREEN_OFF
 * For idle_mode == LIVESTREAM both timer-sections are hidden.
 * ============================================================ */
static void update_section_visibility(void)
{
    bool auto_ss_visible    = (s.pending_idle_mode == IDLE_MODE_SCREENSAVER);
    bool screen_off_visible = (s.pending_idle_mode == IDLE_MODE_SCREEN_OFF);

    if (s.auto_ss_section) {
        if (auto_ss_visible) {
            lv_obj_clear_flag(s.auto_ss_section, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s.auto_ss_section, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s.screen_off_section) {
        if (screen_off_visible) {
            lv_obj_clear_flag(s.screen_off_section, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s.screen_off_section, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ============================================================
 * Build helpers
 * ============================================================ */

static lv_obj_t *build_info_icon_btn(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 28, 28);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, UI_OPA_SURFACE_2, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, ICON_INFO);
    lv_obj_set_style_text_font(icon, &lucide_22, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(icon, UI_OPA_TEXT_SECONDARY, 0);
    lv_obj_center(icon);

    return btn;
}

static lv_obj_t *build_section(lv_obj_t *parent)
{
    lv_obj_t *sec = lv_obj_create(parent);
    lv_obj_remove_style_all(sec);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(sec, UI_COLOR_BG_ELEV_2, 0);
    lv_obj_set_style_bg_opa(sec, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sec, UI_RADIUS_LG, 0);
    lv_obj_set_style_border_color(sec, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(sec, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(sec, 1, 0);
    lv_obj_set_style_pad_all(sec, UI_SPACE_6, 0);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sec, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(sec, UI_SPACE_4, 0);
    lv_obj_clear_flag(sec, LV_OBJ_FLAG_SCROLLABLE);
    return sec;
}

static void build_section_header(lv_obj_t *parent,
                                  const char *title,
                                  lv_event_cb_t info_cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_SPACE_3, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(row);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_font(title_lbl, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(title_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(title_lbl, UI_OPA_TEXT_SECONDARY, 0);
    lv_obj_set_flex_grow(title_lbl, 1);

    if (info_cb) {
        build_info_icon_btn(row, info_cb);
    }
}

/*
 * build_radio_row creates a clickable row with a custom-drawn radio
 * (outer ring + inner dot) and a label.
 * Stores the outer and dot pointers in out_outer / out_dot for later
 * visual updates.
 */
static void build_radio_row(lv_obj_t *parent,
                             const char *label_text,
                             lv_event_cb_t click_cb,
                             int idx,
                             lv_obj_t **out_outer,
                             lv_obj_t **out_dot)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, UI_SPACE_2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_SPACE_4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    /* Outer circle */
    lv_obj_t *outer = lv_obj_create(row);
    lv_obj_remove_style_all(outer);
    lv_obj_set_size(outer, 22, 22);
    lv_obj_set_style_radius(outer, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_opa(outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(outer, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_opa(outer, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(outer, 2, 0);
    lv_obj_clear_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
    if (out_outer) *out_outer = outer;

    /* Inner dot */
    lv_obj_t *dot = lv_obj_create(outer);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    if (out_dot) *out_dot = dot;

    /* Label */
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(lbl, UI_OPA_TEXT_SECONDARY, 0);
}

/* ============================================================
 * CARVILON footer
 * ============================================================ */

static void build_carvilon_footer(lv_obj_t *parent)
{
    /* Hairline separator */
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, lv_pct(60), 1);
    lv_obj_set_style_bg_color(line, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(line, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_margin_top(line, UI_SPACE_6, 0);
    lv_obj_set_style_margin_bottom(line, UI_SPACE_5, 0);

    /* Footer container - centered column */
    lv_obj_t *footer = lv_obj_create(parent);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(footer, UI_SPACE_2, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    /* CARVILON headline */
    lv_obj_t *head = lv_label_create(footer);
    lv_label_set_text(head, "CARVILON");
    lv_obj_set_style_text_font(head, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(head, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(head, UI_OPA_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_letter_space(head, 8, 0);

    /* Sub-headline */
    lv_obj_t *sub = lv_label_create(footer);
    lv_label_set_text(sub, "INTERCOM ESP VIEWER");
    lv_obj_set_style_text_font(sub, UI_FONT_BASE, 0);
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(sub, UI_OPA_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_letter_space(sub, 4, 0);

    /* Version */
    lv_obj_t *ver = lv_label_create(footer);
    lv_label_set_text(ver, "Version 1.0");
    lv_obj_set_style_text_font(ver, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(ver, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(ver, UI_OPA_TEXT_TERTIARY, 0);
}

/* ============================================================
 * Public API: build
 * ============================================================ */

lv_obj_t *scr_settings_build(lv_obj_t *parent, const scr_settings_data_t *data)
{
    memset(&s, 0, sizeof(s));
    s.pending_idle_mode    = data ? data->initial_idle_view_mode       : IDLE_MODE_SCREENSAVER;
    s.pending_auto_ss      = data ? data->initial_auto_screensaver_sec : 60;
    s.pending_screen_off   = data ? data->initial_screen_off_sec       : 0;
    s.pending_brightness   = data ? data->initial_brightness           : 70;
    s.pending_language     = data ? data->initial_language             : LANG_DE;
    s.pending_clock_layout = data ? data->initial_clock_layout         : CLOCK_LAYOUT_VERTICAL;

    /* ----- Root (covers parent) ----- */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(root, UI_COLOR_BG_ELEV, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, UI_RADIUS_2XL, 0);
    lv_obj_set_style_border_color(root, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(root, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_set_style_pad_all(root, UI_SPACE_7, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(root, UI_SPACE_6, 0);

    /* Make the content scrollable - 5 sections + footer can exceed
     * available height on 800x1280 if user opens with all options. */
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);
    s.root = root;

    /* ----- Header: "Einstellungen" + X-Button ----- */
    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_lbl = lv_label_create(header);
    lv_label_set_text(header_lbl, "Einstellungen");
    lv_obj_set_style_text_font(header_lbl, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(header_lbl, UI_COLOR_TEXT, 0);

    lv_obj_t *close_btn = lv_obj_create(header);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 44, 44);
    lv_obj_set_style_radius(close_btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(close_btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(close_btn, UI_OPA_SURFACE_2, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    s.close_btn = close_btn;

    lv_obj_t *close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, ICON_X);
    lv_obj_set_style_text_font(close_icon, &lucide_22, 0);
    lv_obj_set_style_text_color(close_icon, UI_COLOR_TEXT, 0);
    lv_obj_center(close_icon);

    /* ============================================================
     * Sektion 1: Idle-Ansicht (Ruhe-Ansicht)
     * ============================================================ */
    lv_obj_t *sec1 = build_section(root);
    build_section_header(sec1, "Ruhe-Ansicht", tt_idle_cb);
    for (int i = 0; i < IDLE_MODE_COUNT; i++) {
        build_radio_row(sec1, s_idle_mode_presets[i].label,
                         idle_mode_click_cb, i,
                         &s.idle_radios[i], &s.idle_dots[i]);
    }
    update_idle_radios();

    /* ============================================================
     * Sektion 2: Auto-Bildschirmschoner
     * ============================================================ */
    lv_obj_t *sec2 = build_section(root);
    s.auto_ss_section = sec2;
    build_section_header(sec2, "Auto-Bildschirmschoner", tt_auto_ss_cb);
    for (int i = 0; i < AUTO_SS_COUNT; i++) {
        build_radio_row(sec2, s_auto_ss_presets[i].label,
                         auto_ss_click_cb, i,
                         &s.auto_ss_radios[i], &s.auto_ss_dots[i]);
    }
    update_auto_ss_radios();

    /* ============================================================
     * Sektion 3: Bildschirm aus nach
     * ============================================================ */
    lv_obj_t *sec3 = build_section(root);
    s.screen_off_section = sec3;
    build_section_header(sec3, "Bildschirm aus nach", tt_screen_off_cb);
    for (int i = 0; i < SCREEN_OFF_COUNT; i++) {
        build_radio_row(sec3, s_screen_off_presets[i].label,
                         screen_off_click_cb, i,
                         &s.screen_off_radios[i], &s.screen_off_dots[i]);
    }
    update_screen_off_radios();

    /* ============================================================
     * Sektion 4: Helligkeit (slider + live preview, 300ms debounce)
     * ============================================================ */
    lv_obj_t *sec4 = build_section(root);
    build_section_header(sec4, "Helligkeit", tt_brightness_cb);

    lv_obj_t *slider_row = lv_obj_create(sec4);
    lv_obj_remove_style_all(slider_row);
    lv_obj_set_size(slider_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(slider_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slider_row, 0, 0);
    lv_obj_set_flex_flow(slider_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slider_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(slider_row, UI_SPACE_5, 0);
    lv_obj_clear_flag(slider_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *slider = lv_slider_create(slider_row);
    lv_obj_set_height(slider, 12);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, s.pending_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, UI_OPA_SURFACE_2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s.brightness_slider = slider;

    lv_obj_t *value_lbl = lv_label_create(slider_row);
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%d%%", s.pending_brightness);
    lv_label_set_text(value_lbl, vbuf);
    lv_obj_set_style_text_font(value_lbl, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(value_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_width(value_lbl, 70);
    lv_obj_set_style_text_align(value_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    s.brightness_value_lbl = value_lbl;

    /* ============================================================
     * Sektion 5: Sprache
     * ============================================================ */
    lv_obj_t *sec5 = build_section(root);
    build_section_header(sec5, "Sprache", tt_language_cb);
    for (int i = 0; i < LANG_COUNT; i++) {
        build_radio_row(sec5, s_lang_presets[i].label,
                         lang_click_cb, i,
                         &s.lang_radios[i], &s.lang_dots[i]);
    }
    update_lang_radios();

    /* ============================================================
     * Sektion 6: Uhr-Anzeige (S03-09)
     * Immer sichtbar, unabhaengig vom idle_view_mode.
     * ============================================================ */
    lv_obj_t *sec6 = build_section(root);
    build_section_header(sec6, "Uhr-Anzeige", tt_clock_layout_cb);
    for (int i = 0; i < CLOCK_LAYOUT_COUNT; i++) {
        build_radio_row(sec6, s_clock_layout_presets[i].label,
                         clock_layout_click_cb, i,
                         &s.clock_layout_radios[i], &s.clock_layout_dots[i]);
    }
    update_clock_layout_radios();

    /* Initial visibility for the timer-sections based on idle_view_mode */
    update_section_visibility();

    /* ============================================================
     * CARVILON-Footer
     * ============================================================ */
    build_carvilon_footer(root);

    /* ----- Tooltip modal (top of everything) ----- */
    s.tooltip_modal = build_tooltip_modal(parent);

    return root;
}

/* ============================================================
 * Public API: external apply (config.changed echo)
 * ============================================================ */

void scr_settings_apply_external(const scr_settings_snapshot_t *snap)
{
    if (!snap) return;

    s.external_update_in_progress = true;

    s.pending_idle_mode    = snap->idle_view_mode;
    s.pending_auto_ss      = snap->auto_screensaver_sec;
    s.pending_screen_off   = snap->screen_off_sec;
    s.pending_brightness   = snap->brightness;
    s.pending_language     = snap->language;
    s.pending_clock_layout = snap->clock_layout;

    if (s.brightness_slider) {
        lv_slider_set_value(s.brightness_slider, snap->brightness, LV_ANIM_OFF);
    }
    if (s.brightness_value_lbl) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", snap->brightness);
        lv_label_set_text(s.brightness_value_lbl, buf);
    }

    update_idle_radios();
    update_auto_ss_radios();
    update_screen_off_radios();
    update_lang_radios();
    update_clock_layout_radios();
    update_section_visibility();

    s.external_update_in_progress = false;
}

/* ============================================================
 * Public API: callback registration
 * ============================================================ */

void scr_settings_set_brightness_preview_cb(scr_settings_brightness_preview_cb_t cb,
                                            void *user_data)
{
    s.preview_cb = cb;
    s.preview_cb_user_data = user_data;
}

void scr_settings_set_change_handler(scr_settings_change_cb_t cb,
                                      void *user_data)
{
    s.change_cb = cb;
    s.change_cb_user_data = user_data;
}

void scr_settings_set_close_handler(lv_event_cb_t cb, void *user_data)
{
    if (!s.close_btn || !cb) return;
    lv_obj_add_event_cb(s.close_btn, cb, LV_EVENT_CLICKED, user_data);
}
