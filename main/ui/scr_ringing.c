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
#include "stream_pipeline.h"   /* S4-03: canvas attach/detach */

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "SCRRING";


/*
 * Cached pointers, set during scr_ringing_build, consumed by
 * scr_ringing_set_*_handler setters und scr_ringing_show/hide.
 *
 * Single-instance: the overlay is built once in main.c on_got_ip.
 * If the overlay is ever rebuilt, the cache simply gets overwritten
 * and the previous overlay's setters become no-ops on the new one.
 */
static lv_obj_t *s_overlay    = NULL;
static lv_obj_t *s_reject_btn = NULL;
static lv_obj_t *s_unlock_btn = NULL;
static lv_obj_t *s_accept_btn = NULL;
static lv_obj_t *s_sub_label  = NULL;


/* ---------- Bell hero with pulse rings ---------- */
static void build_bell_hero(lv_obj_t *parent)
{
    /* .bell-hero-wrap: Container fuer Bell + 3 Pulse-Rings.
     *
     * S4-01-Fix: Pulse-Rings skalieren auf 2.2x (UI_BELL_HERO_SIZE * 2.2
     * ~= 610px). LV_OBJ_FLAG_OVERFLOW_VISIBLE schaltet das Child-
     * Clipping konzeptionell ab.
     *
     * S4-07 Glow-Fix: OVERFLOW_VISIBLE allein reicht NICHT. In LVGL 9
     * (lv_refr.c:131-141) ist die Kinder-Clip-Area = wrap->coords +
     * wrap->ext_draw_size, und ohne transform_width/_height bleibt
     * ext_draw_size = 0. Folge: Wrap clippt die Kinder trotzdem an
     * seinen 277px-Bounds, der 80px-Hero-Glow wird zum Kasten
     * abgeschnitten. Mit transform_width/_height = 170 wird die
     * Kinder-Clip-Area 277+340 = 617px - genug fuer Pulse-Rings
     * (Layer 617) UND Hero-Glow (Layer 277+162=439). */
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, UI_BELL_HERO_SIZE, UI_BELL_HERO_SIZE);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    /* S4-07: ext_draw_size hochziehen damit die Kinder-Clip-Area gross
     * genug ist fuer Pulse-Scale 2.2x und Hero-Glow 80px. Bounding-Box
     * des Wraps bleibt 277x277 -> KEIN Layout-Shift im content-Flex. */
    lv_obj_set_style_transform_width(wrap, 170, 0);
    lv_obj_set_style_transform_height(wrap, 170, 0);

    /* 3 .bell-pulse rings (border-only circles, accent-soft border).
     * Skalieren via ui_anim_bell_pulse 0.6x -> 2.2x, phasenversetzt
     * je 800ms.
     *
     * S4-06 Pulse-Fix: transform_scale wird in LVGL 9 von
     * lv_obj_calculate_ext_draw_size NICHT mitberechnet (nur shadow_,
     * outline_, transform_width/_height). Folge: Render-Layer der Ring-
     * Box bleibt 277x277, der 2.2x-Output wird beim Compositing in den
     * 277x277-Rahmen geschnitten -> sichtbarer, abgerundeter Kasten um
     * die Glocke (genau das was im Geraete-Log d=0 size=277x277,
     * radius=CIRCLE, ovf_vis=0 erzeugt hat).
     *
     * Wir extenden ext_draw_size manuell via transform_width/_height auf
     * 170px pro Seite (>166 = (2.2-1)/2 * 277). Der Layer ist dann
     * 277+340 = 617x617, der Pulse atmet frei als Kreis nach aussen. */
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
        /* S4-06: ext_draw_size manuell fuer transform_scale 2.2x reservieren */
        lv_obj_set_style_transform_width(ring, 170, 0);
        lv_obj_set_style_transform_height(ring, 170, 0);
        ui_anim_bell_pulse(ring, i * 800);
    }

    /* .bell-hero: glassmorphic Kreis um die Bell. Web-CSS:
     *   bg     rgba(255,255,255,0.08)   ~ 20/255 weiss-opa
     *   border 1px rgba(255,255,255,0.18) ~ 46/255 weiss-opa
     *   shadow 0 0 50px accent-glow + 0 0 100px accent-soft */
    lv_obj_t *hero = lv_obj_create(wrap);
    lv_obj_remove_style_all(hero);
    lv_obj_set_size(hero, UI_BELL_HERO_SIZE, UI_BELL_HERO_SIZE);
    lv_obj_center(hero);
    lv_obj_set_style_radius(hero, UI_RADIUS_FULL, 0);
    lv_obj_set_style_bg_color(hero, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(hero, 20, 0);   /* 0.08 */
    lv_obj_set_style_border_color(hero, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_opa(hero, 46, 0);  /* 0.18 */
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_shadow_color(hero, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_width(hero, 80, 0);
    lv_obj_set_style_shadow_opa(hero, UI_OPA_ACCENT_GLOW, 0);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    /* S4-07: hero-Layer-Reserve auf volle 80px Shadow-Width pinnen.
     * Defaultmaessig berechnet LVGL fuer Shadow nur shadow_width/2 + 1
     * = 41 (siehe lv_obj_draw.c:259-270). 41 reicht knapp fuer den
     * halbtransparenten Aussen-Halo, aber wir wollen den vollen 80px-
     * Glow garantieren, daher explizit pinnen. */
    lv_obj_set_style_transform_width(hero, 80, 0);
    lv_obj_set_style_transform_height(hero, 80, 0);

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

    /* Icon - Lucide font glyph at ~50px (lucide_22 base scaled 2.27x).
     * Buttons sind UI_RING_BTN_SIZE x UI_RING_BTN_SIZE (143 nach S4-01).
     * lucide_88 hat nur ICON_BELL als Glyphe, nicht X/lock/phone, deshalb
     * skalieren wir lucide_22 hoch. Etwas weicher als ein nativer 50px-
     * Font, aber kein zweites Font-Asset noetig. */
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lucide_22, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT, 0);
    lv_obj_center(icon);
    lv_obj_set_style_transform_scale(icon, 580, 0);  /* 256=1.0 -> 580=2.27x (22->50px) */
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
    /* .ringing: Vollbild-Overlay auf dem LVGL-Top-Layer (S4-02b).
     *
     * Architektur:
     *   parent (lv_layer_top()) - ueber allen Screens
     *     overlay (transparent, pad_all=0, OVERFLOW_VISIBLE)
     *       backdrop  full-bleed 800x1280 schwarz opak (Camera-Placeholder)
     *       scrim     full-bleed 800x1280 schwarz 35%
     *       content   full-bleed Frame mit 90/24/24/56 Padding +
     *                 flex column fuer Bell-Hero / Text / Spacer / Actions
     *
     * Warum content separat:
     * - In S4-01b lagen Padding-Werte (90/24/24/56) direkt am overlay.
     *   Damit wurden backdrop/scrim per lv_pct(100) auf die overlay-
     *   Content-Area gerechnet (752x1134 statt 800x1280). Das ergab
     *   einen 24-90 Padding-Rand rundherum, transparent, durch den
     *   die Idle-Komposition (topbar, action-bar, screensaver) durch-
     *   schien. Korrektur: pad_all=0 am overlay-Root, Padding kommt
     *   auf einen dedizierten content-Container der ueber backdrop+
     *   scrim liegt aber wieder selbst full-bleed-Frame ist. */
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);  /* WICHTIG S4-02b */
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    /* OVERFLOW_VISIBLE: pulse rings duerfen ueber overlay-Bounds
     * hinaus rendern, falls Bell-Hero-Wrap an der Kante sitzt. */
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    s_overlay = overlay;

    /* Schicht 1: opake Vollbild-Backdrop. JETZT echte 800x1280 weil
     * overlay kein Padding mehr hat. Camera-Placeholder fuer S5+. */
    lv_obj_t *backdrop = lv_obj_create(overlay);
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, lv_pct(100), lv_pct(100));
    lv_obj_align(backdrop, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);

    /* Schicht 2: 35%-Scrim, ebenfalls full-bleed 800x1280. Solange
     * backdrop schwarz-opak ist, ist der Scrim visuell redundant;
     * darf trotzdem bleiben fuer wenn der echte Camera-Stream
     * spaeter die Backdrop ersetzt. */
    lv_obj_t *scrim = lv_obj_create(overlay);
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, lv_pct(100), lv_pct(100));
    lv_obj_align(scrim, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scrim, 89, 0);  /* 0.35 */
    lv_obj_set_style_border_width(scrim, 0, 0);
    lv_obj_set_style_radius(scrim, 0, 0);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_CLICKABLE);

    /* Schicht 3: content-Frame. Full-bleed Box, aber MIT dem alten
     * Master-Chat-Padding 90/24/24/56 so dass Bell+Text+Actions ihre
     * Abstaende behalten. Transparent. Flex-Column wie frueher der
     * overlay-Container. OVERFLOW_VISIBLE auch hier damit Pulse-Rings
     * die Content-Bounds nach aussen sprengen koennen. */
    lv_obj_t *content = lv_obj_create(overlay);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    /* CSS padding: 90px 24px 56px - top large, sides 24, bottom 56 */
    lv_obj_set_style_pad_top(content, 90, 0);
    lv_obj_set_style_pad_left(content, UI_SPACE_7, 0);
    lv_obj_set_style_pad_right(content, UI_SPACE_7, 0);
    lv_obj_set_style_pad_bottom(content, 56, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* Bell hero - im content-Flex */
    build_bell_hero(content);

    /* .ring-text: text-align center, margin-top space-9 */
    lv_obj_t *txt = lv_obj_create(content);
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
    s_sub_label = sub;

    /* Spacer to push ring-actions to bottom (CSS: margin-top auto) */
    lv_obj_t *spacer = lv_obj_create(content);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    /* .ring-actions: flex row, space-between, gap 16, padding 16 */
    lv_obj_t *actions = lv_obj_create(content);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_hor(actions, UI_SPACE_5, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(actions, UI_SPACE_5, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    /* 3 columns: Ignorieren / Tür auf / Annehmen.
     * S4-01: Briefing nennt bell-off / key / phone. bell-off und key sind
     * nicht in unserer aktuellen lucide_22-Font drin (Sasch regeneriert
     * die Font separat, hier nicht touchen).
     *   - Ignorieren: ICON_X (Kreuz) - vergleichbarer Reject-Marker
     *   - Tuer auf:   ICON_LOCK_OPEN (lock-with-key-shape, semantisch
     *                 naeher am Briefing's "key" als ICON_DOOR_OPEN
     *                 und konsistent mit dem Idle-Action-Bar-Button)
     *   - Annehmen:   ICON_PHONE - matched die Briefing-Spec
     * Alle drei Buttons sind ab S4-01 funktional (kein 'disabled'). */
    s_reject_btn = build_ring_col(actions, ICON_X,         RING_DANGER, false, "Ignorieren");
    s_unlock_btn = build_ring_col(actions, ICON_LOCK_OPEN, RING_WARN,   false, "Tür auf");
    s_accept_btn = build_ring_col(actions, ICON_PHONE,     RING_OK,     false, "Annehmen");

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

void scr_ringing_set_unlock_handler(lv_obj_t *overlay,
                                    lv_event_cb_t cb,
                                    void *user_data)
{
    (void)overlay; /* reserved for future multi-overlay support */
    if (!s_unlock_btn || !cb) return;
    lv_obj_add_event_cb(s_unlock_btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_add_flag(s_unlock_btn, LV_OBJ_FLAG_CLICKABLE);
}

void scr_ringing_set_door_name(const char *door_name)
{
    if (s_sub_label && door_name) {
        lv_label_set_text(s_sub_label, door_name);
    }
}

void scr_ringing_set_accept_handler(lv_obj_t *overlay,
                                     lv_event_cb_t cb,
                                     void *user_data)
{
    (void)overlay;
    if (!s_accept_btn || !cb) return;
    lv_obj_add_event_cb(s_accept_btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_add_flag(s_accept_btn, LV_OBJ_FLAG_CLICKABLE);
}

/* ---------- Show / Hide (instant, S4-03) ---------- *
 *
 * Vorher: 400ms opa-Fade auf s_overlay via style_opa-Anim + completion-
 * Callback fuer HIDDEN-Flag. Hat in LVGL 9 nicht zuverlaessig cascadiert
 * (refr nutzt style_opa_layered fuer Cascade-Skip, nicht style_opa) und
 * die completion_cb feuerte unter Race-Bedingungen nicht.
 *
 * Jetzt: synchroner HIDDEN-Toggle + explizit lv_obj_invalidate. Plus
 * der Stream-Canvas wird beim Show ins Overlay reparented (zwischen
 * backdrop und scrim) und beim Hide zurueck zum stream_view. Die
 * Detach-Operation laeuft auf JEDEM Schliess-Pfad (cancel/reject/accept),
 * weil alle ueber idle_mode_mgr_doorbell_end -> scr_ringing_hide gehen.
 *
 * Z-Order im Overlay nach Show:
 *   index 0   backdrop  opak schwarz - Boden falls Stream aus ist
 *   index 1   canvas    Live-Stream Vollbild (reparented)
 *   index 2   scrim     0.35 schwarz - Lesbarkeits-Abdunklung
 *   index 3   content   Bell + Text + 3 Action-Buttons
 */

void scr_ringing_show(void)
{
    if (!s_overlay) {
        ESP_LOGW(TAG, "RING_SHOW: s_overlay not built yet");
        return;
    }
    ESP_LOGI(TAG, "RING_SHOW");

    /* Stream-Canvas ins Overlay reparenten und auf Index 1 stellen
     * (ueber backdrop, unter scrim + content). */
    lv_obj_t *canvas = stream_pipeline_attach_to_overlay(s_overlay);
    if (canvas) {
        lv_obj_move_to_index(canvas, 1);
    }

    /* Top-Layer-Foreground (defensiv) + sichtbar + invalidieren. */
    lv_obj_move_foreground(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_overlay);
}

void scr_ringing_hide(void)
{
    if (!s_overlay) return;
    if (lv_obj_has_flag(s_overlay, LV_OBJ_FLAG_HIDDEN)) return;

    ESP_LOGI(TAG, "RING_HIDE");

    /* WICHTIG: Detach BEVOR HIDDEN gesetzt wird. Sonst waere der Canvas
     * fuer einen Tick Kind eines versteckten Containers und der Idle-
     * Stream zwischendrin schwarz. */
    stream_pipeline_detach_from_overlay();

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    /* Parent invalidieren damit das was unter dem Overlay liegt
     * (idle_screen) sauber neu gerendert wird. */
    lv_obj_t *parent = lv_obj_get_parent(s_overlay);
    if (parent) lv_obj_invalidate(parent);
}
