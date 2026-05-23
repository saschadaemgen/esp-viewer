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
 *                    5 Buttons - Mittelgruppe eng, Aussen-Buttons abgesetzt;
 *                    Inhalt sitzt per Flex-CENTER vertikal mittig, S5-22 -
 *                    pad_top == pad_bottom, symmetrischer Atem oben + unten)
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


/* ---------- Toolbar-Button-Builders (S5-19 final, idle-aligned) ----------
 *
 * Drei dedizierte Helper-Funktionen fuer die drei visuell unterschied-
 * lichen Button-Typen der Klingel-Toolbar:
 *
 *   build_glass_btn   - Annehmen + Ablehnen: feine Glass-Surface
 *                       (UI_COLOR_SURFACE @ UI_OPA_SURFACE_2) + 1px-
 *                       Hairline-Border. Icon farb-/opa-konfigurierbar.
 *                       Genau wie die scr_idle-Glass-Action-Buttons.
 *   build_primary_btn - Tuer (Mitte): exakt der Idle-primary-Stil
 *                       (build_action_btn_primary in scr_idle.c) -
 *                       Accent-Blau Gradient + Accent-Glow. Konsistent
 *                       zur Tuer-Aktion im Idle.
 *   build_text_btn    - MUTE + REC: transparent (kein bg, kein border,
 *                       kein shadow), nur das Icon mit gedaempfter Opa.
 *                       Das beigefuegte Label wird vom Caller drumherum
 *                       gebaut (col-Container mit Button + Label).
 *
 * Alle Buttons rund (UI_RADIUS_FULL) und clickable. Icons aus lucide_22
 * mit transform_scale fuer groessere Darstellungen (Pivot mittig 11,11).
 */

static void place_icon(lv_obj_t *btn, const char *icon,
                       lv_color_t color, lv_opa_t opa, int32_t scale)
{
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_font(lbl, &lucide_22, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_opa(lbl, opa, 0);
    lv_obj_center(lbl);
    if (scale != 256) {
        lv_obj_set_style_transform_scale(lbl, scale, 0);
        lv_obj_set_style_transform_pivot_x(lbl, 11, 0);
        lv_obj_set_style_transform_pivot_y(lbl, 11, 0);
    }
}

/* Glass-Button (Annehmen, Ablehnen). Neutrale Flaeche, sparsam
 * konturiert. Icon-Farbe/Opa caller-konfigurierbar (Ablehnen ueberlaedt
 * das auf rot, Annehmen bleibt weiss-gedaempft). */
static lv_obj_t *build_glass_btn(lv_obj_t *parent, int32_t size,
                                  const char *icon,
                                  lv_color_t icon_color, lv_opa_t icon_opa,
                                  int32_t icon_scale)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, UI_OPA_SURFACE_2, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(btn, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    place_icon(btn, icon, icon_color, icon_opa, icon_scale);
    return btn;
}

/* Primary-Tuer-Button. EXAKT der Idle-primary-Stil aus scr_idle.c
 * (build_action_btn_primary). Accent-Blau-Gradient + Accent-Glow -
 * der einzig farbig gefuellte Button der Toolbar, hierarchisch klar
 * die Hauptaktion. Idle-Glow ist hier explizit erlaubt fuer Konsistenz
 * mit dem Idle-Tuer-Button. */
static lv_obj_t *build_primary_btn(lv_obj_t *parent, int32_t size,
                                    const char *icon, int32_t icon_scale)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, size, size);
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
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    place_icon(btn, icon, UI_COLOR_TEXT_ON_ACCENT, LV_OPA_COVER, icon_scale);
    return btn;
}

/* Text-Button (MUTE, REC). Komplett transparent, kein border, kein
 * shadow - der Caller stapelt drumherum einen col-Container mit Button
 * + Label. Icon mit gedaempfter Opa. */
static lv_obj_t *build_text_btn(lv_obj_t *parent, int32_t size,
                                 const char *icon,
                                 lv_color_t icon_color, lv_opa_t icon_opa,
                                 int32_t icon_scale)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    place_icon(btn, icon, icon_color, icon_opa, icon_scale);
    return btn;
}

/* Aussen-Block (MUTE links, REC rechts). Col-Container mit dem text_btn
 * oben und einem kleinen uppercase-Label darunter. Eng (~2 px gap).
 * Caller bekommt den Button-Pointer fuer die Handler-Verdrahtung. */
static lv_obj_t *build_outer_block(lv_obj_t *parent, int32_t btn_size,
                                    const char *icon,
                                    lv_color_t icon_color, lv_opa_t icon_opa,
                                    const char *label_text,
                                    lv_color_t label_color, lv_opa_t label_opa,
                                    lv_obj_t **out_btn)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 2, 0);   /* eng: Button + Label nah beieinander */
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    *out_btn = build_text_btn(col, btn_size, icon, icon_color, icon_opa, 256);

    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, UI_FONT_XS, 0);   /* 12 px */
    lv_obj_set_style_text_color(lbl, label_color, 0);
    lv_obj_set_style_text_opa(lbl, label_opa, 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);

    return col;
}

/* Vertikaler Hairline-Trenner (1 px Linie, dezent). Trennt MUTE +
 * REC von der Mittelgruppe ohne harte Border. */
static lv_obj_t *build_vert_hairline(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *hl = lv_obj_create(parent);
    lv_obj_remove_style_all(hl);
    lv_obj_set_size(hl, 1, height);
    lv_obj_set_style_bg_color(hl, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(hl, UI_OPA_HAIRLINE_STRONG, 0);
    lv_obj_set_style_border_width(hl, 0, 0);
    lv_obj_clear_flag(hl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hl, LV_OBJ_FLAG_CLICKABLE);
    return hl;
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
    lv_obj_set_style_bg_color(header, UI_COLOR_BG, 0);     /* viel Schwarz */
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(header, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(header, UI_SPACE_2, 0);       /* 8 */
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(header, 2, 0);                 /* eng zwischen den 2 Zeilen */
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);
    s_header = header;

    /* Zeile 1 - "ES KLINGELT" klein, uppercase, letter-spaced, gedaempft.
     * UI_FONT_XS = lv_font_montserrat_12. Apple-style overline-label. */
    lv_obj_t *line1 = lv_label_create(header);
    lv_label_set_text(line1, "ES KLINGELT");
    lv_obj_set_style_text_font(line1, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(line1, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(line1, UI_OPA_TEXT_TERTIARY, 0);
    lv_obj_set_style_text_letter_space(line1, 2, 0);

    /* Zeile 2 - DoorName gross, hell. UI_FONT_XL = montserrat_22.
     * Opa knapp unter cover (~0.92, 235/255) - leichter "Apple-Hell". */
    lv_obj_t *line2 = lv_label_create(header);
    lv_label_set_text(line2,
                      (data && data->door_name) ? data->door_name : "Hauseingang");
    lv_obj_set_style_text_font(line2, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(line2, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(line2, 235, 0);
    s_status_label = line2;

    /* S5-18 Klingel-Toolbar unten (140 px, sicherer Bereich). Dunkler
     * opaker Hintergrund, feine Hairline oben. Keine runden Ecken (mit
     * Header oben + Toolbar unten ist es ein klares Top/Bottom-Frame
     * um das Video - Apple-flat).
     *
     * S5-22: Flex-Column mit LV_FLEX_ALIGN_CENTER auf Hauptachse +
     * symmetrisches pad_ver. Der btn_row sitzt vertikal mittig in der
     * Toolbar, Abstand oben == Abstand unten. Loest die Asymmetrie
     * an der Wurzel - die manuellen +N-px-Offsets aus S5-20/S5-21
     * sind weg, weil die Zentrierung jede Toolbar-Hoehe symmetrisch
     * aufteilt. */
    lv_obj_t *toolbar = lv_obj_create(overlay);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, UI_SCREEN_W, UI_KLINGEL_TOOLBAR_H);
    lv_obj_align(toolbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(toolbar, UI_COLOR_BG, 0);    /* S5-18 viel Schwarz */
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(toolbar, UI_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_opa(toolbar, UI_OPA_HAIRLINE, 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_ver(toolbar, UI_SPACE_5, 0);   /* 16 oben+unten symmetrisch */
    lv_obj_set_style_pad_hor(toolbar, UI_SPACE_7, 0);   /* 24 Seitenrand */
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_CLICKABLE);
    s_toolbar = toolbar;

    /* Button-Row: flex row, space-between (S5-18 C2 regroup). Drei
     * children: Ignorieren (links), center_group (Annehmen/Tuer/Ablehnen
     * eng zusammen, Mitte), Record (rechts). Die zentrale 3er-Gruppe
     * bekommt eigene flex-row mit kleinem pad_column = visuell als eine
     * Gruppe; Ignorieren + Record sitzen weit aussen abgesetzt. */
    lv_obj_t *btn_row = lv_obj_create(toolbar);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, lv_pct(100), UI_KLINGEL_BTN_LG);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_CLICKABLE);

    /* Child 1 (links aussen): MUTE-Block (Button + Label) + Hairline
     * rechts. Eigener sub-flex-row Container damit die beiden
     * (col-Block + Hairline) als eine Einheit am linken Rand sitzen.
     * MUTE-Block selber ist ein col mit Icon oben + Label unten. */
    lv_obj_t *left_block = lv_obj_create(btn_row);
    lv_obj_remove_style_all(left_block);
    lv_obj_set_size(left_block, LV_SIZE_CONTENT, UI_KLINGEL_BTN_LG);
    lv_obj_set_style_bg_opa(left_block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_block, 0, 0);
    lv_obj_set_flex_flow(left_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_block, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_block, UI_SPACE_5, 0); /* 16 zwischen MUTE und Hairline */
    lv_obj_clear_flag(left_block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(left_block, LV_OBJ_FLAG_CLICKABLE);

    build_outer_block(left_block, UI_KLINGEL_BTN_SM,
                       ICON_BELL_OFF,
                       UI_COLOR_TEXT, 128,    /* Icon weiss ~0.50 */
                       "MUTE",
                       UI_COLOR_TEXT, 102,    /* Label weiss ~0.40 */
                       &s_btn_ignore);

    build_vert_hairline(left_block, UI_KLINGEL_BTN_LG - 8); /* ~88 px */

    /* Child 2 (Mitte): Hauptgruppe Annehmen/Tuer/Ablehnen eng beieinander
     * als eigener sub-flex-row Container. LV_SIZE_CONTENT = Container
     * passt sich an die Buttons-Breite an. */
    lv_obj_t *center_group = lv_obj_create(btn_row);
    lv_obj_remove_style_all(center_group);
    lv_obj_set_size(center_group, LV_SIZE_CONTENT, UI_KLINGEL_BTN_LG);
    lv_obj_set_style_bg_opa(center_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center_group, 0, 0);
    lv_obj_set_flex_flow(center_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_group, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center_group, UI_SPACE_3, 0); /* 12, eng */
    lv_obj_clear_flag(center_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(center_group, LV_OBJ_FLAG_CLICKABLE);

    /* Annehmen (Glass-neutral, Icon ICON_PHONE weiss gedaempft). */
    s_btn_accept = build_glass_btn(center_group, UI_KLINGEL_BTN_MD,
                                    ICON_PHONE,
                                    UI_COLOR_TEXT, 216, /* ~0.85 */
                                    325);               /* 22 -> ~28 px */

    /* Tuer (Mitte, gross, Idle-primary-Blau + Idle-Glow). Einzige
     * farbige Flaeche der Toolbar - Hauptaktion klar markiert. */
    s_btn_door = build_primary_btn(center_group, UI_KLINGEL_BTN_LG,
                                    ICON_LOCK_OPEN,
                                    415);               /* 22 -> ~36 px */

    /* Ablehnen (Glass-neutral wie Annehmen, NUR ICON_X ist rot).
     * Fluache bleibt neutral - nicht zu viel Rot in der Toolbar. */
    s_btn_reject = build_glass_btn(center_group, UI_KLINGEL_BTN_MD,
                                    ICON_X,
                                    UI_COLOR_DANGER, 242, /* ~0.95 */
                                    325);

    /* Child 3 (rechts aussen): Hairline links + REC-Block (Button +
     * Label) rechts. Spiegelbildlich zum left_block. */
    lv_obj_t *right_block = lv_obj_create(btn_row);
    lv_obj_remove_style_all(right_block);
    lv_obj_set_size(right_block, LV_SIZE_CONTENT, UI_KLINGEL_BTN_LG);
    lv_obj_set_style_bg_opa(right_block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_block, 0, 0);
    lv_obj_set_flex_flow(right_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_block, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_block, UI_SPACE_5, 0); /* 16 */
    lv_obj_clear_flag(right_block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(right_block, LV_OBJ_FLAG_CLICKABLE);

    build_vert_hairline(right_block, UI_KLINGEL_BTN_LG - 8);

    build_outer_block(right_block, UI_KLINGEL_BTN_SM,
                       ICON_CIRCLE,
                       UI_COLOR_DANGER, 179,  /* Icon rot ~0.70 */
                       "REC",
                       UI_COLOR_DANGER, 153,  /* Label rot ~0.60 */
                       &s_btn_record);

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
    /* s_status_label zeigt im S5-19-Header NUR den Door-Namen (Zeile 2);
     * die Overline "ES KLINGELT" ist statisch und gehoert zu Zeile 1. */
    if (!s_status_label || !door_name) return;
    lv_label_set_text(s_status_label, door_name);
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
