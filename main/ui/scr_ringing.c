/*
 * scr_ringing.c - Klingel-Screen (S5-16 Plan B: Toolbar unten, kein
 * UI-Overlay ueber dem Stream).
 *
 * Layout (waehrend Klingelns):
 *   y =    0..1080   Stream Vollbreite (stream_pipeline fullscreen=true,
 *                    KLINGEL_STREAM_H = STREAM_H - KLINGEL_TOOLBAR_H)
 *   y = 1080..1280   Klingel-Toolbar (LVGL, im "sicheren" Bereich den
 *                    der Stream nicht beschreibt - kein Doppel-Render
 *                    wie beim alten PPA-Overlay-Versuch S5-08..S5-15)
 *
 * Toolbar (5 Buttons + 1-Zeile Status-Label, LTR):
 *
 *  Klingelt - Hauseingang
 *  [Ignorieren] [Annehmen] [TUER (gross+pulse)] [Ablehnen] [Record]
 *      72           104             144              104        72
 *
 * Hauptaktion (Tuer-Auf, mittig+gross) pulsiert dezent via lv_anim auf
 * bg_opa - cheap LVGL-Render in der 144x144 Region, im sicheren
 * Toolbar-Bereich (Stream beruehrt y<1080 nicht). Kein transform_*
 * (das war der CPU-Killer S5-09).
 *
 * Stufe 1 verdraht Tuer/Annehmen/Ablehnen wie heute via main.c-Setters.
 * Ignorieren + Record sind aktive Buttons mit Stub-Handlern - Funktion
 * folgt in Stufe 2 (Mute = zurueck zu Idle, kein Reject-Signal) und
 * Stufe 4 (Audio-Aufnahme).
 */

#include "scr_ringing.h"
#include "scr_idle.h"          /* is_screensaver_mode for hide-restore */
#include "ui_tokens.h"
#include "lucide_22.h"
#include "stream_pipeline.h"   /* set_visible + set_fullscreen */

#include "lvgl.h"
#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "SCRRING";


/*
 * Cached pointers, set during scr_ringing_build, consumed by
 * scr_ringing_set_*_handler setters + scr_ringing_show/hide.
 */
static lv_obj_t *s_overlay      = NULL;
static lv_obj_t *s_toolbar      = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_btn_ignore   = NULL;
static lv_obj_t *s_btn_accept   = NULL;
static lv_obj_t *s_btn_door     = NULL;
static lv_obj_t *s_btn_reject   = NULL;
static lv_obj_t *s_btn_record   = NULL;


/* ---------- Toolbar-Button-Builder ----------
 *
 * Alle 5 Buttons teilen sich diese Helper-Funktion. Stil-Variabilitaet
 * (Groesse, Farbe, Glow, Icon-Scale) per kring_btn_style_t.
 */
typedef struct {
    const char *icon;       /* lucide_22 UTF-8 string, oder NULL */
    int32_t     size;       /* Aussen-Durchmesser */
    lv_color_t  bg;
    lv_opa_t    bg_opa;
    lv_color_t  shadow;
    lv_opa_t    shadow_opa;
    lv_color_t  icon_color;
    int32_t     icon_scale; /* 256 = 1.0, e.g. 700 = 2.73x (22 -> ~60 px) */
} kring_btn_style_t;

static lv_obj_t *build_kring_btn(lv_obj_t *parent, const kring_btn_style_t *style)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, style->size, style->size);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, style->bg, 0);
    lv_obj_set_style_bg_opa(btn, style->bg_opa, 0);
    lv_obj_set_style_shadow_color(btn, style->shadow, 0);
    lv_obj_set_style_shadow_width(btn, 20, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 8, 0);
    lv_obj_set_style_shadow_opa(btn, style->shadow_opa, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

    if (style->icon) {
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, style->icon);
        lv_obj_set_style_text_font(icon, &lucide_22, 0);
        lv_obj_set_style_text_color(icon, style->icon_color, 0);
        lv_obj_center(icon);
        if (style->icon_scale != 256) {
            lv_obj_set_style_transform_scale(icon, style->icon_scale, 0);
            lv_obj_set_style_transform_pivot_x(icon, 11, 0); /* lucide_22 = 22 px, pivot mittig */
            lv_obj_set_style_transform_pivot_y(icon, 11, 0);
        }
    }

    return btn;
}


/* ---------- Tuer-Button Puls-Animation ----------
 *
 * bg-opa zycelt 200..255 (78%..100%) jeweils 750 ms hin + zurueck =
 * 1.5 s Cycle, infinite, ease-in-out. Repaintet nur die 144x144
 * Tuer-Button-Region im sicheren Toolbar-Bereich (y>=1080, vom Stream
 * nicht beruehrt). Subtiler "atmender" Glow ohne Pixel-Transform - kein
 * CPU-Killer wie das alte transform_rotation-Wackeln (S5-09).
 */
static void door_pulse_exec_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void start_door_pulse(lv_obj_t *door_btn)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, door_btn);
    lv_anim_set_exec_cb(&a, door_pulse_exec_cb);
    lv_anim_set_values(&a, 200, 255);
    lv_anim_set_duration(&a, 750);
    lv_anim_set_playback_duration(&a, 750);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}


/* ---------- Top-level build ---------- */
lv_obj_t *scr_ringing_build(lv_obj_t *parent, const scr_ringing_data_t *data)
{
    /* Overlay = Full-Screen-Container auf lv_layer_top. Transparent ueber
     * dem ganzen Display. Nur die Toolbar unten ist opak; der Rest des
     * Overlays gibt den Stream durch (kein Overlay-bg). */
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    s_overlay = overlay;

    /* Toolbar bottom 200 px. Opaker dunkler Hintergrund (anthrazit) mit
     * subtiler Hairline-Border oben + abgerundeten oberen Ecken (iPad-
     * Stil). clip_corner damit Buttons innerhalb der Toolbar nicht
     * ueber die runden Ecken hinaus rendern. */
    lv_obj_t *toolbar = lv_obj_create(overlay);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, UI_SCREEN_W, UI_KLINGEL_TOOLBAR_H);
    lv_obj_align(toolbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(toolbar, UI_COLOR_BG_ELEV, 0);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(toolbar, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(toolbar, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(toolbar, UI_RADIUS_2XL, 0);
    lv_obj_set_style_clip_corner(toolbar, true, 0);
    lv_obj_set_style_pad_top(toolbar, UI_SPACE_3, 0);
    lv_obj_set_style_pad_bottom(toolbar, UI_SPACE_2, 0);
    lv_obj_set_style_pad_hor(toolbar, UI_SPACE_5, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(toolbar, UI_SPACE_2, 0);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_CLICKABLE);
    s_toolbar = toolbar;

    /* Status-Label "Klingelt   <DoorName>". UI_FONT_XL = 22 px Montserrat.
     * Sekundaer-Text-Opa (62 %). Mittig (Flex-Container alignt center). */
    lv_obj_t *status = lv_label_create(toolbar);
    char buf[96];
    snprintf(buf, sizeof(buf), "Klingelt   %s",
             (data && data->door_name) ? data->door_name : "Hauseingang");
    lv_label_set_text(status, buf);
    lv_obj_set_style_text_font(status, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(status, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(status, UI_OPA_TEXT_SECONDARY, 0);
    s_status_label = status;

    /* Button-Row: flex row, space-evenly. Hoehe = max-Button-Hoehe (Tuer 144),
     * kleinere zentrieren vertikal. */
    lv_obj_t *btn_row = lv_obj_create(toolbar);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, lv_pct(100), UI_KLINGEL_BTN_LG);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, UI_SPACE_3, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_CLICKABLE);

    /* Button 1 LTR: Ignorieren (klein, grau/glass). Stub-Icon ICON_BELL
     * - in Stufe 3 ersetzbar durch bell-off oder mute. */
    kring_btn_style_t st_ignore = {
        .icon = ICON_BELL, .size = UI_KLINGEL_BTN_SM,
        .bg = UI_COLOR_SURFACE, .bg_opa = UI_OPA_SURFACE_3,
        .shadow = UI_COLOR_HAIRLINE, .shadow_opa = UI_OPA_HAIRLINE_STRONG,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 325,    /* 22 -> ~28 px */
    };
    s_btn_ignore = build_kring_btn(btn_row, &st_ignore);

    /* Button 2: Annehmen (mittel, gruen). */
    kring_btn_style_t st_accept = {
        .icon = ICON_PHONE, .size = UI_KLINGEL_BTN_MD,
        .bg = UI_COLOR_OK, .bg_opa = LV_OPA_COVER,
        .shadow = UI_COLOR_OK, .shadow_opa = UI_OPA_OK_GLOW,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 465,    /* 22 -> ~40 px */
    };
    s_btn_accept = build_kring_btn(btn_row, &st_accept);

    /* Button 3 (Mitte): Tuer-Oeffnen (gross, primary-blau, PULSIERT). */
    kring_btn_style_t st_door = {
        .icon = ICON_LOCK_OPEN, .size = UI_KLINGEL_BTN_LG,
        .bg = UI_COLOR_ACCENT, .bg_opa = LV_OPA_COVER,
        .shadow = UI_COLOR_ACCENT, .shadow_opa = UI_OPA_ACCENT_GLOW,
        .icon_color = UI_COLOR_TEXT_ON_ACCENT,
        .icon_scale = 700,    /* 22 -> ~60 px */
    };
    s_btn_door = build_kring_btn(btn_row, &st_door);

    /* Button 4: Ablehnen (mittel, rot). */
    kring_btn_style_t st_reject = {
        .icon = ICON_X, .size = UI_KLINGEL_BTN_MD,
        .bg = UI_COLOR_DANGER, .bg_opa = LV_OPA_COVER,
        .shadow = UI_COLOR_DANGER, .shadow_opa = UI_OPA_DANGER_GLOW,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 465,
    };
    s_btn_reject = build_kring_btn(btn_row, &st_reject);

    /* Button 5 LTR: Record (klein, rot). Lucide_22 hat keinen Record-Icon
     * (Stufe 3 importiert ihn). Stub-Look: roter Button mit kleinem weissem
     * Innen-Quadrat (klassischer Stop/Record-Look). */
    kring_btn_style_t st_record = {
        .icon = NULL, .size = UI_KLINGEL_BTN_SM,
        .bg = UI_COLOR_DANGER, .bg_opa = LV_OPA_COVER,
        .shadow = UI_COLOR_DANGER, .shadow_opa = UI_OPA_DANGER_GLOW,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 256,
    };
    s_btn_record = build_kring_btn(btn_row, &st_record);
    lv_obj_t *rec_dot = lv_obj_create(s_btn_record);
    lv_obj_remove_style_all(rec_dot);
    lv_obj_set_size(rec_dot, 22, 22);
    lv_obj_set_style_radius(rec_dot, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(rec_dot, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(rec_dot, LV_OPA_COVER, 0);
    lv_obj_center(rec_dot);
    lv_obj_clear_flag(rec_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rec_dot, LV_OBJ_FLAG_CLICKABLE);

    /* Tuer-Puls-Anim starten (im sicheren Toolbar-Bereich, kein Stream-
     * Konflikt). */
    start_door_pulse(s_btn_door);

    return overlay;
}


/* ---------- Handler setters ---------- */
void scr_ringing_set_reject_handler(lv_obj_t *overlay,
                                    lv_event_cb_t cb,
                                    void *user_data)
{
    (void)overlay;
    if (!s_btn_reject || !cb) return;
    lv_obj_add_event_cb(s_btn_reject, cb, LV_EVENT_CLICKED, user_data);
}

void scr_ringing_set_unlock_handler(lv_obj_t *overlay,
                                    lv_event_cb_t cb,
                                    void *user_data)
{
    (void)overlay;
    if (!s_btn_door || !cb) return;
    lv_obj_add_event_cb(s_btn_door, cb, LV_EVENT_CLICKED, user_data);
}

void scr_ringing_set_accept_handler(lv_obj_t *overlay,
                                     lv_event_cb_t cb,
                                     void *user_data)
{
    (void)overlay;
    if (!s_btn_accept || !cb) return;
    lv_obj_add_event_cb(s_btn_accept, cb, LV_EVENT_CLICKED, user_data);
}

void scr_ringing_set_ignore_handler(lv_obj_t *overlay,
                                     lv_event_cb_t cb,
                                     void *user_data)
{
    (void)overlay;
    if (!s_btn_ignore || !cb) return;
    lv_obj_add_event_cb(s_btn_ignore, cb, LV_EVENT_CLICKED, user_data);
}

void scr_ringing_set_record_handler(lv_obj_t *overlay,
                                     lv_event_cb_t cb,
                                     void *user_data)
{
    (void)overlay;
    if (!s_btn_record || !cb) return;
    lv_obj_add_event_cb(s_btn_record, cb, LV_EVENT_CLICKED, user_data);
}

void scr_ringing_set_door_name(const char *door_name)
{
    if (!s_status_label || !door_name) return;
    char buf[96];
    snprintf(buf, sizeof(buf), "Klingelt   %s", door_name);
    lv_label_set_text(s_status_label, buf);
}


/* ---------- Show / Hide ---------- *
 *
 * Reihenfolge im show: erst Idle-Chrome verstecken (Topbar, Frame,
 * Action-Bar), dann stream_pipeline auf fullscreen (Stream Vollbreite
 * y=0..1080), dann overlay sichtbar machen. Die Toolbar liegt im
 * sicheren Bereich y=1080..1280 - LVGL malt sie einmal und keiner
 * uebermalt sie (kein Doppel-Render-Konflikt mit dem Stream).
 */

void scr_ringing_show(void)
{
    if (!s_overlay) {
        ESP_LOGW(TAG, "RING_SHOW: s_overlay not built yet");
        return;
    }
    ESP_LOGI(TAG, "RING_SHOW");

    scr_idle_set_actions_visible(false);
    scr_idle_set_topbar_visible(false);
    scr_idle_set_design_frame_visible(false);

    stream_pipeline_set_visible(true);
    stream_pipeline_set_fullscreen(true);

    lv_obj_move_foreground(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_overlay);
}

void scr_ringing_hide(void)
{
    if (!s_overlay) return;
    if (lv_obj_has_flag(s_overlay, LV_OBJ_FLAG_HIDDEN)) return;

    ESP_LOGI(TAG, "RING_HIDE");

    stream_pipeline_set_fullscreen(false);

    scr_idle_set_actions_visible(true);
    scr_idle_set_topbar_visible(true);
    scr_idle_set_design_frame_visible(true);
    stream_pipeline_set_visible(!scr_idle_is_screensaver_mode());

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    /* Parent invalidieren damit das was unter dem Overlay liegt
     * (idle_screen) sauber neu gerendert wird. */
    lv_obj_t *parent = lv_obj_get_parent(s_overlay);
    if (parent) lv_obj_invalidate(parent);
}
