/*
 * scr_ringing.c - Klingel-Screen (S5-18 Apple-Style: Header oben +
 * Toolbar unten, beide im sicheren Bereich, kein PPA-Overlay).
 *
 * Layout (waehrend Klingelns):
 *   y =    0..88     Klingel-Header (UI_KLINGEL_HEADER_H = 88, LVGL,
 *                    "Es klingelt - <DoorName>" gross zentriert)
 *   y =   88..1140   Stream Vollbreite (KLINGEL_VIDEO_H = 1052,
 *                    zwischen Header und Toolbar)
 *   y = 1140..1280   Klingel-Toolbar (UI_KLINGEL_TOOLBAR_H = 140, LVGL,
 *                    5 Buttons - Mittelgruppe eng, Aussen-Buttons abgesetzt)
 *
 * Header + Toolbar liegen in sicheren Bereichen die der Stream nicht
 * beschreibt - kein Doppel-Render-Konflikt wie beim alten PPA-Overlay-
 * Versuch S5-08..S5-15. Reines LVGL, stabil wie die Idle-Topbar.
 *
 * Buttons (S5-18, LTR mit Mittelgruppe + abgesetzten Aussen):
 *  [Ignorieren]  ........  [Annehmen] [TUER] [Ablehnen]  ........  [Record]
 *       56                      72       96       72                    56
 *   glas-grau                gruen   primary   rot                 rot+circle
 *  ICON_BELL_OFF          ICON_PHONE  ICON_LOCK_OPEN  ICON_X     ICON_CIRCLE
 *
 * Hauptaktion (Tuer-Auf, mittig+gross) ist STATISCH (S5-17). Keine
 * kontinuierlichen LVGL-Animationen in der Toolbar - Direct-FB-Setup
 * vertraegt sie nicht (fps-Killer).
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
static lv_obj_t *s_header       = NULL;   /* S5-18: obere Status-Bar */
static lv_obj_t *s_status_label = NULL;   /* "Es klingelt - <DoorName>" im Header */
static lv_obj_t *s_toolbar      = NULL;
static lv_obj_t *s_btn_ignore   = NULL;
static lv_obj_t *s_btn_accept   = NULL;
static lv_obj_t *s_btn_door     = NULL;
static lv_obj_t *s_btn_reject   = NULL;
static lv_obj_t *s_btn_record   = NULL;


/* ---------- Toolbar-Button-Builder (S5-17 refined) ----------
 *
 * Alle 5 Buttons teilen sich diese Helper-Funktion. Stil-Variabilitaet
 * (Groesse, Farbe, Gradient, Glow, Icon-Scale) per kring_btn_style_t.
 *
 * Refine S5-17: dezentere Schatten (width 12 statt 20, ofs_y 4 statt 8) -
 * weniger laut, mehr iPad-Stil. Optionaler bg-Gradient (Tuer-Button) ueber
 * grad_color != bg + grad_dir.
 */
typedef struct {
    const char    *icon;            /* lucide_22 UTF-8 string, oder NULL */
    int32_t        size;            /* Aussen-Durchmesser */
    lv_color_t     bg;
    lv_opa_t       bg_opa;
    lv_color_t     bg_grad;         /* Gradient-Zielfarbe (oder = bg fuer solid) */
    lv_grad_dir_t  grad_dir;        /* LV_GRAD_DIR_NONE = solid */
    lv_color_t     shadow;
    lv_opa_t       shadow_opa;
    lv_color_t     icon_color;
    int32_t        icon_scale;      /* 256 = 1.0, lucide_22 base = 22 px */
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
    if (style->grad_dir != LV_GRAD_DIR_NONE) {
        lv_obj_set_style_bg_grad_color(btn, style->bg_grad, 0);
        lv_obj_set_style_bg_grad_dir(btn, style->grad_dir, 0);
    }
    lv_obj_set_style_shadow_color(btn, style->shadow, 0);
    lv_obj_set_style_shadow_width(btn, 12, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 4, 0);
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


/* ---------- Tuer-Button (statisch, S5-17) ----------
 *
 * S5-16 hatte einen bg_opa-Puls auf dem Tuer-Button (1.5 s Cycle infinite).
 * Sollte cheap sein weil im sicheren Toolbar-Bereich und nur die 144x144
 * Region invalidiert. Geraete-Befund S5-17: fps brach auf ~4 ein, CPU
 * oben. Der LVGL-Repaint-Zyklus dieser pro-Frame-Anim ist im Direct-FB-
 * Setup zu teuer (vermutlich Interaktion mit dem trans_sem/VSYNC-Sync der
 * Stream-Pipeline). Puls KOMPLETT raus, Tuer-Button bleibt statisch.
 */


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

    /* S5-18 Klingel-Header oben (88 px, sicherer Bereich). Idle-Topbar-
     * Stil: dunkler opaker Hintergrund, feine Hairline-Border unten.
     * Eine grosse Status-Zeile zentriert: "Es klingelt - <DoorName>".
     * Stream beruehrt y<88 nicht -> kein Doppel-Render-Konflikt. */
    lv_obj_t *header = lv_obj_create(overlay);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, UI_SCREEN_W, UI_KLINGEL_HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_BG_ELEV, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(header, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);
    s_header = header;

    lv_obj_t *status = lv_label_create(header);
    char buf[96];
    snprintf(buf, sizeof(buf), "Es klingelt   %s",
             (data && data->door_name) ? data->door_name : "Hauseingang");
    lv_label_set_text(status, buf);
    lv_obj_set_style_text_font(status, UI_FONT_3XL, 0);     /* 26 px */
    lv_obj_set_style_text_color(status, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(status, UI_OPA_TEXT, 0);
    lv_obj_center(status);
    s_status_label = status;

    /* S5-18 Klingel-Toolbar unten (140 px, sicherer Bereich). Dunkler
     * opaker Hintergrund, feine Hairline oben. Keine runden Ecken (mit
     * Header oben + Toolbar unten ist es ein klares Top/Bottom-Frame
     * um das Video - Apple-flat). */
    lv_obj_t *toolbar = lv_obj_create(overlay);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, UI_SCREEN_W, UI_KLINGEL_TOOLBAR_H);
    lv_obj_align(toolbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(toolbar, UI_COLOR_BG_ELEV, 0);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(toolbar, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(toolbar, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_ver(toolbar, UI_SPACE_5, 0);   /* 16 oben+unten */
    lv_obj_set_style_pad_hor(toolbar, UI_SPACE_7, 0);   /* 24 Seitenrand */
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_CLICKABLE);
    s_toolbar = toolbar;

    /* Button-Row: flex row, space-evenly. Hoehe = max-Button-Hoehe (Tuer 96),
     * kleinere zentrieren vertikal. (Regrouping in S5-18 C2 folgt.) */
    lv_obj_t *btn_row = lv_obj_create(toolbar);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, lv_pct(100), UI_KLINGEL_BTN_LG);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, UI_SPACE_5, 0); /* 16 */
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_CLICKABLE);

    /* Button 1 LTR: Ignorieren / Stumm. Klein 56, glas-grau, dezent. */
    kring_btn_style_t st_ignore = {
        .icon = ICON_BELL_OFF, .size = UI_KLINGEL_BTN_SM,
        .bg = UI_COLOR_SURFACE, .bg_opa = UI_OPA_SURFACE_3,
        .bg_grad = UI_COLOR_SURFACE, .grad_dir = LV_GRAD_DIR_NONE,
        .shadow = UI_COLOR_HAIRLINE, .shadow_opa = UI_OPA_HAIRLINE,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 256,    /* 22 px native, kein scale (passt in 56 mit 17 px Luft) */
    };
    s_btn_ignore = build_kring_btn(btn_row, &st_ignore);

    /* Button 2: Annehmen. Mittel 72, gruen, dezenter Glow. */
    kring_btn_style_t st_accept = {
        .icon = ICON_PHONE, .size = UI_KLINGEL_BTN_MD,
        .bg = UI_COLOR_OK, .bg_opa = LV_OPA_COVER,
        .bg_grad = UI_COLOR_OK, .grad_dir = LV_GRAD_DIR_NONE,
        .shadow = UI_COLOR_OK, .shadow_opa = 80,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 325,    /* 22 -> ~28 px */
    };
    s_btn_accept = build_kring_btn(btn_row, &st_accept);

    /* Button 3 (Mitte): Tuer-Oeffnen. Gross 96, primary-blue mit subtilem
     * Vertikal-Gradient (light-blau oben -> accent-blau unten, wie der
     * Idle-primary-Button). Klar Haupt-Aktion, aber nicht uebermaechtig.
     * Statisch (kein Puls - Direct-FB-Anim ist perf-Killer S5-17). */
    kring_btn_style_t st_door = {
        .icon = ICON_LOCK_OPEN, .size = UI_KLINGEL_BTN_LG,
        .bg = UI_COLOR_ACCENT_LIGHT, .bg_opa = LV_OPA_COVER,
        .bg_grad = UI_COLOR_ACCENT, .grad_dir = LV_GRAD_DIR_VER,
        .shadow = UI_COLOR_ACCENT, .shadow_opa = 96,
        .icon_color = UI_COLOR_TEXT_ON_ACCENT,
        .icon_scale = 415,    /* 22 -> ~36 px */
    };
    s_btn_door = build_kring_btn(btn_row, &st_door);

    /* Button 4: Ablehnen. Mittel 72, rot, dezenter Glow. */
    kring_btn_style_t st_reject = {
        .icon = ICON_X, .size = UI_KLINGEL_BTN_MD,
        .bg = UI_COLOR_DANGER, .bg_opa = LV_OPA_COVER,
        .bg_grad = UI_COLOR_DANGER, .grad_dir = LV_GRAD_DIR_NONE,
        .shadow = UI_COLOR_DANGER, .shadow_opa = 80,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 325,
    };
    s_btn_reject = build_kring_btn(btn_row, &st_reject);

    /* Button 5 LTR: Record. Klein 56, rot, ICON_CIRCLE (weisser
     * gefuellter Kreis = klassischer Record-Look). Sehr dezent. */
    kring_btn_style_t st_record = {
        .icon = ICON_CIRCLE, .size = UI_KLINGEL_BTN_SM,
        .bg = UI_COLOR_DANGER, .bg_opa = LV_OPA_COVER,
        .bg_grad = UI_COLOR_DANGER, .grad_dir = LV_GRAD_DIR_NONE,
        .shadow = UI_COLOR_DANGER, .shadow_opa = 64,
        .icon_color = UI_COLOR_TEXT,
        .icon_scale = 256,    /* 22 px native, klein gefuellter Kreis */
    };
    s_btn_record = build_kring_btn(btn_row, &st_record);

    /* S5-17: KEIN Tuer-Puls mehr - hat im Direct-FB-Setup auf 4 fps
     * gedrueckt (Geraete-Befund). Tuer-Button bleibt statisch. */

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
    snprintf(buf, sizeof(buf), "Es klingelt   %s", door_name);
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
