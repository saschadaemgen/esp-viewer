/*
 * scr_screensaver.c - Bildschirmschoner-View Implementation
 */

#include "scr_screensaver.h"
#include "ui_tokens.h"
#include "lucide_22.h"
#include "montserrat_200.h"
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

    /* Big clock */
    lv_obj_t *clock_label;

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

    /* Clock - only redraw on minute change (cheap to compute, but
     * skipping the label_set saves a bunch of LVGL invalidations). */
    char tbuf[8];
    time_sync_format_time(tbuf, sizeof(tbuf), s.language);

    /* montserrat_200 hat nur Ziffern und Doppelpunkt im Range.
     * Bei unsynced (time_sync_format_time -> "--:--") wuerde LVGL
     * Kaesten-Fallback rendern. Bis NTP synced -> leerer String. */
    if (!time_sync_is_synced()) {
        tbuf[0] = 0;
    }

    if (s.clock_label) {
        lv_label_set_text(s.clock_label, tbuf);
    }

    /* Date - only redraw on day change */
    if (time_sync_is_synced()) {
        time_t now = 0;
        /* localtime via the same path time_sync uses internally;
         * we just compare cached wday/mday. */
        time(&now);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        if (tm_local.tm_mday != s.last_mday ||
            tm_local.tm_wday != s.last_wday) {
            s.last_mday = tm_local.tm_mday;
            s.last_wday = tm_local.tm_wday;
            rerender_date_now();
        }
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
    s.last_minute = -1;
    s.last_wday = -1;
    s.last_mday = -1;

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

    /* --- Clock (BIG) --- */
    lv_obj_t *clock = lv_label_create(root);
    /* Leer-Initial: montserrat_200 hat Range nur Ziffern+Doppelpunkt,
     * "--:--" wuerde Kaesten zeigen. tick_cb fuellt nach NTP-Sync. */
    lv_label_set_text(clock, "");
    lv_obj_set_style_text_font(clock, &montserrat_200, 0);
    lv_obj_set_style_text_color(clock, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(clock, LV_OPA_COVER, 0);
    /* Master-Chat: letter-spacing 0.02em - LVGL hat lv_obj_set_style_text_letter_space (px) */
    lv_obj_set_style_text_letter_space(clock, 4, 0);
    lv_obj_set_style_margin_bottom(clock, 16, 0);
    s.clock_label = clock;

    /* --- Date (small, under clock) --- */
    lv_obj_t *date = lv_label_create(root);
    lv_label_set_text(date, "");
    lv_obj_set_style_text_font(date, UI_FONT_LG, 0);   /* 18px Montserrat */
    lv_obj_set_style_text_color(date, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(date, UI_OPA_TEXT_SECONDARY, 0);
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
