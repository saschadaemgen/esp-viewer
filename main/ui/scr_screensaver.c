/*
 * scr_screensaver.c - Bildschirmschoner-View Implementation
 */

#include "scr_screensaver.h"
#include "ui_tokens.h"
#include "lucide_22.h"
#include "montserrat_200.h"
#include "montserrat_140.h"   /* S03-09 horizontal clock font */
#include "services/time_sync.h"

#include "lvgl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Module state
 * ============================================================ */

typedef struct {
    lv_obj_t *root;

    /* Pixel-style vertical clock: HH on its own line, MM below
     * with negative pad_row so the two digit-blocks overlap into
     * the Montserrat-200 line-height whitespace. */
    lv_obj_t *clock_group;
    lv_obj_t *clock_hour;
    lv_obj_t *clock_minute;

    /* Horizontal classic clock (S03-09): a single label "HH:MM"
     * in montserrat_140. Sibling of clock_group; only one of the
     * two is visible at a time, controlled by current_layout. */
    lv_obj_t *clock_horizontal;
    clock_layout_t current_layout;

    /* Date below clock */
    lv_obj_t *date_label;

    /* Weather row container + children */
    lv_obj_t *weather_row;
    lv_obj_t *weather_icon;
    lv_obj_t *weather_text;       /* "12°C Bewölkt" */

    /* Unread badge container + children */
    lv_obj_t *badge_row;
    lv_obj_t *badge_icon;
    lv_obj_t *badge_text;         /* "3 verpasst" */

    /* State */
    language_t language;
    int unread_count;

    /* Last rendered time/date to skip redundant label updates */
    int last_hour;
    int last_minute;
    int last_wday;
    int last_mday;
} scr_screensaver_state_t;

static scr_screensaver_state_t s = {0};

/* ============================================================
 * Lucide icon-code -> font glyph mapping
 * ============================================================ */

static const char *weather_icon_for(const char *code)
{
    if (!code || code[0] == 0) return ICON_CLOUD;

    if (strcmp(code, "sun")             == 0) return ICON_SUN;
    if (strcmp(code, "cloud")           == 0) return ICON_CLOUD;
    if (strcmp(code, "cloud-rain")      == 0) return ICON_CLOUD_RAIN;
    if (strcmp(code, "cloud-snow")      == 0) return ICON_CLOUD_SNOW;
    if (strcmp(code, "cloud-fog")       == 0) return ICON_CLOUD_FOG;
    if (strcmp(code, "cloud-lightning") == 0) return ICON_CLOUD_LIGHTNING;
    if (strcmp(code, "cloud-drizzle")   == 0) return ICON_CLOUD_DRIZZLE;
    if (strcmp(code, "cloud-hail")      == 0) return ICON_CLOUD_HAIL;
    if (strcmp(code, "wind")            == 0) return ICON_WIND;

    return ICON_CLOUD;
}

/* ============================================================
 * Pulse animation for the badge
 * ============================================================ */

static void badge_opa_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void start_badge_pulse(lv_obj_t *target)
{
    if (!target) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, target);
    lv_anim_set_values(&a, 179, 255);   /* 0.70 .. 1.00 */
    lv_anim_set_duration(&a, UI_DUR_BREATHE);
    lv_anim_set_playback_duration(&a, UI_DUR_BREATHE);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, badge_opa_anim_cb);
    lv_anim_start(&a);
}

static void stop_badge_pulse(lv_obj_t *target)
{
    if (!target) return;
    lv_anim_delete(target, badge_opa_anim_cb);
    lv_obj_set_style_opa(target, LV_OPA_COVER, 0);
}

/* ============================================================
 * Tick: update clock + date once per second
 * ============================================================ */

static void rerender_date_now(void)
{
    if (!s.date_label) return;
    char dbuf[64];
    time_sync_format_date(dbuf, sizeof(dbuf), s.language);
    lv_label_set_text(s.date_label, dbuf);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;

    /* Bei nicht-synced: alle Clock-Labels leer. montserrat_200 und
     * montserrat_140 haben beide nur Ziffern + Doppelpunkt im Range,
     * "--" wuerde Kaesten zeigen. */
    if (!time_sync_is_synced()) {
        if (s.clock_hour)       lv_label_set_text(s.clock_hour, "");
        if (s.clock_minute)     lv_label_set_text(s.clock_minute, "");
        if (s.clock_horizontal) lv_label_set_text(s.clock_horizontal, "");
        return;
    }

    time_t now = 0;
    time(&now);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    bool hour_changed = (tm_local.tm_hour != s.last_hour);
    bool min_changed  = (tm_local.tm_min  != s.last_minute);

    /* Stunden + Minuten getrennt rendern, nur bei Aenderung labeln
     * (Minute aendert sich 60x/h, Stunde 1x/h). Spart LVGL-invalidate.
     * Wir fuettern beide Layouts unabhaengig vom aktuell sichtbaren -
     * der Cost ist minimal (Setter no-op wenn HIDDEN war's nicht im
     * Display-Buffer), und ein Switch ohne Re-Render des aktiven
     * Texts ist garantiert konsistent. */
    if (hour_changed) {
        s.last_hour = tm_local.tm_hour;
        char hh[4];
        snprintf(hh, sizeof(hh), "%02d", tm_local.tm_hour);
        if (s.clock_hour) lv_label_set_text(s.clock_hour, hh);
    }
    if (min_changed) {
        s.last_minute = tm_local.tm_min;
        char mm[4];
        snprintf(mm, sizeof(mm), "%02d", tm_local.tm_min);
        if (s.clock_minute) lv_label_set_text(s.clock_minute, mm);
    }

    /* Horizontal "HH:MM" - rendert beide auf einmal. Update wenn
     * Stunde ODER Minute sich geaendert hat, oder beim ersten Tick
     * (last_minute == -1 nach Build/Unsynced-Phase). */
    if (s.clock_horizontal && (hour_changed || min_changed)) {
        char hhmm[8];
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d",
                 tm_local.tm_hour, tm_local.tm_min);
        lv_label_set_text(s.clock_horizontal, hhmm);
    }

    /* Date - only redraw on day change */
    if (tm_local.tm_mday != s.last_mday ||
        tm_local.tm_wday != s.last_wday) {
        s.last_mday = tm_local.tm_mday;
        s.last_wday = tm_local.tm_wday;
        rerender_date_now();
    }
}

/* ============================================================
 * Build
 * ============================================================ */

lv_obj_t *scr_screensaver_build(lv_obj_t *parent, language_t lang)
{
    memset(&s, 0, sizeof(s));
    s.language = lang;
    s.unread_count = 0;
    s.last_hour = -1;
    s.last_minute = -1;
    s.last_wday = -1;
    s.last_mday = -1;
    s.current_layout = CLOCK_LAYOUT_VERTICAL;

    /* Root - fills parent (modes-container slot) */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(root, UI_COLOR_STREAM_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, UI_RADIUS_2XL, 0);
    lv_obj_set_style_clip_corner(root, true, 0);
    lv_obj_set_style_border_color(root, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(root, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root,
                          LV_FLEX_ALIGN_CENTER,   /* main: vertical centering */
                          LV_FLEX_ALIGN_CENTER,   /* cross: horizontal centering */
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    s.root = root;

    /* --- Pixel-Style Clock --- */
    /* Sub-container im Flex-Column-Modus mit negativem pad_row.
     * Damit ueberlappen die zwei montserrat_200-Labels in deren
     * vertikalem Whitespace (Line-Height >> Glyph-Height bei 200px).
     * Sasch kann den Wert ggf. nachjustieren wenn visuelle Luecke
     * zu gross oder zu klein. */
    lv_obj_t *clock_grp = lv_obj_create(root);
    lv_obj_remove_style_all(clock_grp);
    lv_obj_set_size(clock_grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock_grp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_grp,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    /* Row-Gap +20: Minuten mit etwas Luft unter die Stunden.
     * Test-Iteration (18.05.): -20 zu eng, 0 zu hoch, -10 falsche
     * Richtung, +20 ist die richtige Mischung (Minuten ruecken
     * nach unten, leichter visueller Atem zwischen den Bloecken). */
    lv_obj_set_style_pad_row(clock_grp, 20, 0);
    lv_obj_set_style_bg_opa(clock_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_grp, 0, 0);
    lv_obj_set_style_pad_all(clock_grp, 0, 0);
    lv_obj_clear_flag(clock_grp, LV_OBJ_FLAG_SCROLLABLE);
    s.clock_group = clock_grp;

    /* Stunden-Label - oben */
    lv_obj_t *hour = lv_label_create(clock_grp);
    lv_label_set_text(hour, "");
    lv_obj_set_style_text_font(hour, &montserrat_200, 0);
    lv_obj_set_style_text_color(hour, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(hour, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(hour, 4, 0);
    lv_obj_set_style_text_align(hour, LV_TEXT_ALIGN_CENTER, 0);
    s.clock_hour = hour;

    /* Minuten-Label - unter Stunden */
    lv_obj_t *minute = lv_label_create(clock_grp);
    lv_label_set_text(minute, "");
    lv_obj_set_style_text_font(minute, &montserrat_200, 0);
    lv_obj_set_style_text_color(minute, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(minute, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(minute, 4, 0);
    lv_obj_set_style_text_align(minute, LV_TEXT_ALIGN_CENTER, 0);
    s.clock_minute = minute;

    /* --- Horizontal classic clock (S03-09) ---
     * Sibling-Label im selben Flex-Column wie clock_group. Beide
     * sind im Boot-Build "im Flex", einer ist HIDDEN. Toggle via
     * scr_screensaver_set_clock_layout() flippt nur die Visibility.
     * Default: HIDDEN (Vertikal aktiv).
     *
     * montserrat_140 hat das gleiche Range-Layout wie montserrat_200
     * (0x30-0x3A Ziffern + Doppelpunkt). Doppelpunkt darf hier
     * gerendert werden, anders als beim Bildschirmschoner-Vertikal
     * wo nur Ziffern erlaubt sind. */
    lv_obj_t *hclock = lv_label_create(root);
    lv_label_set_text(hclock, "");
    lv_obj_set_style_text_font(hclock, &montserrat_140, 0);
    lv_obj_set_style_text_color(hclock, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(hclock, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(hclock, 4, 0);
    lv_obj_set_style_text_align(hclock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(hclock, LV_OBJ_FLAG_HIDDEN);
    s.clock_horizontal = hclock;

    /* --- Date (small, under clock) --- */
    lv_obj_t *date = lv_label_create(root);
    lv_label_set_text(date, "");
    lv_obj_set_style_text_font(date, UI_FONT_LG, 0);   /* 18px Montserrat */
    lv_obj_set_style_text_color(date, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(date, UI_OPA_TEXT_SECONDARY, 0);
    /* 50px Abstand zur Minuten-Zeile. Vorheriger 70px war zu viel
     * Luft (Test 18.05.). */
    lv_obj_set_style_margin_top(date, 50, 0);
    lv_obj_set_style_margin_bottom(date, 32, 0);
    s.date_label = date;

    /* --- Weather row --- */
    lv_obj_t *weather = lv_obj_create(root);
    lv_obj_remove_style_all(weather);
    lv_obj_set_size(weather, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(weather, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather, 0, 0);
    lv_obj_set_flex_flow(weather, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weather, 8, 0);
    lv_obj_clear_flag(weather, LV_OBJ_FLAG_SCROLLABLE);
    s.weather_row = weather;

    lv_obj_t *wicon = lv_label_create(weather);
    lv_label_set_text(wicon, ICON_CLOUD);
    lv_obj_set_style_text_font(wicon, &lucide_22, 0);
    lv_obj_set_style_text_color(wicon, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(wicon, UI_OPA_TEXT_SECONDARY, 0);
    s.weather_icon = wicon;

    lv_obj_t *wtext = lv_label_create(weather);
    lv_label_set_text(wtext, "");
    lv_obj_set_style_text_font(wtext, UI_FONT_BASE, 0);
    lv_obj_set_style_text_color(wtext, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(wtext, UI_OPA_TEXT_SECONDARY, 0);
    s.weather_text = wtext;

    /* Initial: weather row hidden until set_weather is called */
    lv_obj_add_flag(weather, LV_OBJ_FLAG_HIDDEN);

    /* --- Unread badge --- */
    lv_obj_t *badge = lv_obj_create(root);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_style_pad_ver(badge, 6, 0);
    lv_obj_set_style_radius(badge, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(badge, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(badge, UI_OPA_SURFACE_2, 0);
    lv_obj_set_style_border_color(badge, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(badge, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_margin_top(badge, 24, 0);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(badge, 6, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    s.badge_row = badge;

    lv_obj_t *bicon = lv_label_create(badge);
    lv_label_set_text(bicon, ICON_BELL);
    lv_obj_set_style_text_font(bicon, &lucide_22, 0);
    lv_obj_set_style_text_color(bicon, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(bicon, UI_OPA_TEXT_SECONDARY, 0);
    lv_obj_set_style_transform_scale(bicon, 208, 0); /* 256=1.0, 208 ~= 0.81 (18/22) */
    s.badge_icon = bicon;

    lv_obj_t *btext = lv_label_create(badge);
    lv_label_set_text(btext, "");
    lv_obj_set_style_text_font(btext, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(btext, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(btext, UI_OPA_TEXT_SECONDARY, 0);
    s.badge_text = btext;

    /* Badge hidden until count > 0 */
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);

    /* Initial render of clock + date - may show "--:--" / "" until NTP synced */
    tick_cb(NULL);

    /* 1-second LVGL timer - LVGL runs it on its own task with display_lock held */
    lv_timer_create(tick_cb, 1000, NULL);

    return root;
}

/* ============================================================
 * Setters
 * ============================================================ */

void scr_screensaver_set_language(language_t lang)
{
    s.language = lang;

    /* Force a redraw of date AND badge text (de/en differ). */
    s.last_mday = -1;
    s.last_wday = -1;
    rerender_date_now();

    /* Rebuild badge text if visible */
    if (s.unread_count > 0) {
        scr_screensaver_set_unread_count(s.unread_count);
    }
}

void scr_screensaver_set_clock_layout(clock_layout_t layout)
{
    if (s.current_layout == layout) return;
    s.current_layout = layout;

    if (layout == CLOCK_LAYOUT_HORIZONTAL) {
        if (s.clock_group)      lv_obj_add_flag(s.clock_group, LV_OBJ_FLAG_HIDDEN);
        if (s.clock_horizontal) lv_obj_clear_flag(s.clock_horizontal, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s.clock_group)      lv_obj_clear_flag(s.clock_group, LV_OBJ_FLAG_HIDDEN);
        if (s.clock_horizontal) lv_obj_add_flag(s.clock_horizontal, LV_OBJ_FLAG_HIDDEN);
    }
}

void scr_screensaver_set_weather(int temp_c,
                                  const char *condition_text,
                                  const char *icon_code)
{
    if (!s.weather_row) return;

    /* Update icon glyph */
    if (s.weather_icon) {
        lv_label_set_text(s.weather_icon, weather_icon_for(icon_code));
    }

    /* Update temperature + text. Example: "12°C Bewölkt" */
    if (s.weather_text) {
        char buf[96];
        const char *cond = (condition_text && condition_text[0] != 0)
                                ? condition_text : "";
        /* "°" is UTF-8 0xC2 0xB0 */
        snprintf(buf, sizeof(buf), "%d\xc2\xb0""C %s", temp_c, cond);
        lv_label_set_text(s.weather_text, buf);
    }

    lv_obj_clear_flag(s.weather_row, LV_OBJ_FLAG_HIDDEN);
}

void scr_screensaver_set_unread_count(int count)
{
    s.unread_count = count;

    if (!s.badge_row) return;

    if (count <= 0) {
        stop_badge_pulse(s.badge_row);
        lv_obj_add_flag(s.badge_row, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (s.badge_text) {
        char buf[32];
        if (s.language == LANG_EN) {
            snprintf(buf, sizeof(buf), "%d missed", count);
        } else {
            snprintf(buf, sizeof(buf), "%d verpasst", count);
        }
        lv_label_set_text(s.badge_text, buf);
    }

    lv_obj_clear_flag(s.badge_row, LV_OBJ_FLAG_HIDDEN);

    /* (Re-)start pulse animation. Idempotent because we delete the
     * previous one first. */
    stop_badge_pulse(s.badge_row);
    start_badge_pulse(s.badge_row);
}
