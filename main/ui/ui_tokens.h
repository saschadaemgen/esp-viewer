/*
 * ui_tokens.h - KOENIGSLIGA Design-System fuer LVGL
 *
 * ESP-Saison 2 Tag 3+ (Design-Token-Vorbereitung)
 *
 * Dieses File ist die SINGLE SOURCE OF TRUTH fuer alle visuellen
 * Werte (Farben, Abstaende, Radien, Schriften, Animationen) auf dem
 * ESP-Viewer. Es spiegelt 1:1 die tokens.css aus dem Web-Viewer
 * (library/tokens.css) wider.
 *
 * VEREINHEITLICHUNG MIT WEB-VIEWER:
 *   - Selbe Farben (#3d7bff, #e84a44, #f0962a, #2cc66a)
 *   - Selbes 4px-Spacing-Grid
 *   - Selbe Radii (6/9/12/16/20/22/28px)
 *   - Selbe Easings (Spring, Out, In-Out)
 *   - Selbe Animation-Durations
 *
 * WAS NICHT MAPPT:
 *   - backdrop-filter blur (LVGL hat kein Shader-Backend)
 *     -> ersetzt durch solide Surface mit angepasstem Alpha
 *   - color-mix() in oklab (CSS-runtime)
 *     -> pre-computed im Code falls noetig
 *   - SVG-Icons
 *     -> LVGL Symbol-Font oder einzeln als C-Array-Assets
 *
 * DARK-THEME ist Default (wie im Web-Viewer).
 * Light-Theme wuerde ueber eine zweite Header-Variante kommen,
 * wenn ueberhaupt noetig.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * SURFACE COLORS - Dark Theme (Default)
 * Spiegelt :root[data-theme="dark"] aus tokens.css
 * ============================================================ */

/* Room (page background ausserhalb device-shell) */
#define UI_COLOR_ROOM             lv_color_hex(0x000000)

/* App-level Background */
#define UI_COLOR_BG               lv_color_hex(0x08090c)
#define UI_COLOR_BG_ELEV          lv_color_hex(0x0e1014)
#define UI_COLOR_BG_ELEV_2        lv_color_hex(0x14161c)

/* Stream-Viewport (bleibt dark in beiden Themes - Video-Slot) */
#define UI_COLOR_STREAM_BG        lv_color_hex(0x050608)

/* Hairlines (1px borders, dark theme = white-on-alpha)
 * In LVGL setzen wir das ueber Opacity-Werte separat. */
#define UI_COLOR_HAIRLINE         lv_color_hex(0xffffff)
#define UI_OPA_HAIRLINE           ((lv_opa_t)15)   /* 0.06 * 255 */
#define UI_OPA_HAIRLINE_STRONG    ((lv_opa_t)25)   /* 0.10 * 255 */

/* Glass surfaces (translucent layers, button bg) */
#define UI_COLOR_SURFACE          lv_color_hex(0xffffff)
#define UI_OPA_SURFACE_1          ((lv_opa_t)10)   /* 0.04 */
#define UI_OPA_SURFACE_2          ((lv_opa_t)15)   /* 0.06 */
#define UI_OPA_SURFACE_3          ((lv_opa_t)25)   /* 0.10 */
#define UI_OPA_SURFACE_HOVER      ((lv_opa_t)20)   /* 0.08 */

/* Frosted-glass sheet background (history sheet) - kein blur
 * im LVGL, daher solide mit hoher Alpha */
#define UI_COLOR_SHEET_BG         lv_color_hex(0x16161a)
#define UI_OPA_SHEET_BG           ((lv_opa_t)219)  /* 0.86 */

#define UI_COLOR_MODAL_BG         lv_color_hex(0x16161a)
#define UI_OPA_MODAL_BG           ((lv_opa_t)239)  /* 0.94 */

/* Scrim behind sheets/modals */
#define UI_COLOR_SCRIM            lv_color_hex(0x000000)
#define UI_OPA_SCRIM              ((lv_opa_t)115)  /* 0.45 */


/* ============================================================
 * TEXT
 * ============================================================ */

#define UI_COLOR_TEXT             lv_color_hex(0xffffff)
#define UI_OPA_TEXT               LV_OPA_COVER          /* 1.00 */
#define UI_OPA_TEXT_SECONDARY     ((lv_opa_t)158)       /* 0.62 */
#define UI_OPA_TEXT_TERTIARY      ((lv_opa_t)107)       /* 0.42 */
#define UI_OPA_TEXT_QUATERNARY    ((lv_opa_t)56)        /* 0.22 */

/* Text auf Accent-Fills */
#define UI_COLOR_TEXT_ON_ACCENT   lv_color_hex(0xffffff)


/* ============================================================
 * ACCENTS - KOENIGSLIGA-Brand-Farben (theme-konstant)
 * ============================================================ */

/* Primary Brand - Tuer-Auf, Links, Focus */
#define UI_COLOR_ACCENT           lv_color_hex(0x3d7bff)
#define UI_COLOR_ACCENT_LIGHT     lv_color_hex(0x6395ff)  /* +18% mix to white */
#define UI_COLOR_ACCENT_SOFT      lv_color_hex(0x3d7bff)
#define UI_OPA_ACCENT_SOFT        ((lv_opa_t)46)         /* 0.18 */
#define UI_OPA_ACCENT_GLOW        ((lv_opa_t)128)        /* 0.50 */

/* Danger - Ignorieren, destruktiv */
#define UI_COLOR_DANGER           lv_color_hex(0xe84a44)
#define UI_OPA_DANGER_SOFT        ((lv_opa_t)38)         /* 0.15 */
#define UI_OPA_DANGER_GLOW        ((lv_opa_t)128)        /* 0.50 */

/* Warn - Tuer-Auf im Ringing (caution) */
#define UI_COLOR_WARN             lv_color_hex(0xf0962a)
#define UI_OPA_WARN_SOFT          ((lv_opa_t)38)
#define UI_OPA_WARN_GLOW          ((lv_opa_t)128)

/* OK - Annehmen, Online, Success */
#define UI_COLOR_OK               lv_color_hex(0x2cc66a)
#define UI_OPA_OK_SOFT            ((lv_opa_t)38)
#define UI_OPA_OK_GLOW            ((lv_opa_t)128)


/* ============================================================
 * TYPOGRAPHY
 * ------------------------------------------------------------
 * Web-Viewer nutzt system-ui (System-Font).
 * LVGL hat Montserrat im Build (Saison-1 Stream-UI).
 * Font-Sizes mappen wir auf die naechste Montserrat-Groesse.
 *
 * Verfuegbare Montserrat-Sizes in unserem Build:
 *   12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 36
 * (genau welche enabled ist menuconfig-abhaengig)
 * ============================================================ */

/* Web-Token (CSS)    -> LVGL-Font (Montserrat)
 *   fs-xs   11px     -> 12 (kleinste)
 *   fs-sm   12px     -> 12
 *   fs-base 14px     -> 14
 *   fs-md   15px     -> 16
 *   fs-lg   17px     -> 18
 *   fs-xl   22px     -> 22
 *   fs-2xl  28px     -> 28
 *   fs-3xl  36px     -> 36  (Klingelt-Headline)
 *
 * Achtung: nicht alle Sizes sind per default kompiliert.
 * Vor erstem Build muessen wir in menuconfig pruefen:
 *   Component config -> LVGL configuration -> Font usage
 */

/*
 * UI_FONT_BASE/LG/XL nutzen unsere eigenen Montserrat-Fonts mit
 * Latin-1-Erweiterung (Umlaute + Grad). Generiert via lvgl.io-Converter.
 * UI_FONT_XS/SM/MD/2XL/3XL nutzen weiterhin die LVGL-Built-ins, weil
 * dort heute keine Umlaute auftauchen (Status-Labels, Headlines etc).
 */

#include "montserrat_14.h"
#include "montserrat_18.h"
#include "montserrat_22.h"

#define UI_FONT_XS                &lv_font_montserrat_12
#define UI_FONT_SM                &lv_font_montserrat_12
#define UI_FONT_BASE              &montserrat_14
#define UI_FONT_MD                &lv_font_montserrat_16
#define UI_FONT_LG                &montserrat_18
#define UI_FONT_XL                &montserrat_22
#define UI_FONT_2XL               &lv_font_montserrat_26
#define UI_FONT_3XL               &lv_font_montserrat_26

/* Font-Weights kommen nicht direkt aus Montserrat in LVGL
 * (LVGL-Montserrat hat nur eine Schnittstaerke, semibold-ish).
 * Wir nutzen sie konzeptionell fuer spaetere Bold/Italic-Versions. */


/* ============================================================
 * SPACING (4px-Grid - exakt wie tokens.css)
 * ============================================================ */

#define UI_SPACE_1                4
#define UI_SPACE_2                8
#define UI_SPACE_3                12
#define UI_SPACE_4                14
#define UI_SPACE_5                16
#define UI_SPACE_6                20
#define UI_SPACE_7                24
#define UI_SPACE_8                28
#define UI_SPACE_9                32
#define UI_SPACE_10               40
#define UI_SPACE_12               48
#define UI_SPACE_14               56
#define UI_SPACE_16               64
#define UI_SPACE_20               80
#define UI_SPACE_24               96


/* ============================================================
 * BORDER RADIUS
 * ============================================================ */

#define UI_RADIUS_XS              6      /* tags, small chips */
#define UI_RADIUS_SM              9      /* squircle icon bubbles */
#define UI_RADIUS_MD              12     /* segment buttons */
#define UI_RADIUS_LG              16     /* cards, list groups */
#define UI_RADIUS_XL              20     /* big cards, modal */
#define UI_RADIUS_2XL             22     /* stream slot */
#define UI_RADIUS_3XL             28     /* bottom sheet */
#define UI_RADIUS_DEVICE          44     /* device shell */
#define UI_RADIUS_FULL            LV_RADIUS_CIRCLE


/* ============================================================
 * MOTION
 * ------------------------------------------------------------
 * CSS easings sind cubic-bezier - LVGL hat eigene Path-Funktionen.
 * Wir mappen so nah wie moeglich:
 *
 *   ease-spring  cubic-bezier(0.32, 0.72, 0, 1)
 *      -> LVGL hat lv_anim_path_overshoot fuer leichten Bounce.
 *      -> Fuer puren Spring nutzen wir lv_anim_path_ease_out
 *         als gute Approximation.
 *
 *   ease-out     cubic-bezier(0.2, 0.7, 0.2, 1)
 *      -> lv_anim_path_ease_out
 *
 *   ease-in-out  cubic-bezier(0.4, 0, 0.2, 1)
 *      -> lv_anim_path_ease_in_out
 *
 * Durations 1:1 aus tokens.css uebernommen.
 * ============================================================ */

#define UI_DUR_FAST               120      /* ms */
#define UI_DUR_BASE               200
#define UI_DUR_SLOW               320
#define UI_DUR_SHEET              360
#define UI_DUR_OVERLAY            240

/* Animation-Durations fuer die Spezial-Animationen */
#define UI_DUR_BREATHE            2400     /* breathing dot im Stream-Hint */
/* UI_DUR_BELL_PULSE entfernt S4-09: Pulse-Ringe komplett aus dem Ringing-
 * Screen geflogen (Soft-Shadow + 3-Ring-Scale in SW war Wuerger). */
/* Glocken-Pendel: eine volle Hin-und-Her-Schwingung in 1400ms,
 * jede "Seite" (0 -> +10 deg -> 0 oder 0 -> -10 deg -> 0) ~700ms.
 * Briefing S4-09: "+/- 8-12 Grad, ~600-800ms hin/her". */
#define UI_DUR_BELL_WOBBLE        1400     /* Glocken-Wackeln im Ringing */


/* ============================================================
 * SCREEN-SPEZIFISCHE DIMENSIONEN
 * Display: 800x1280 portrait
 * ------------------------------------------------------------
 * Idle-Screen Grid (vom Web-Viewer):
 *   topbar:  64px
 *   stream:  1fr (flex)
 *   actions: 96px
 *   padding: UI_SPACE_4 (14px)
 *   gap:     10px
 * ============================================================ */

#define UI_SCREEN_W               800
#define UI_SCREEN_H               1280

#define UI_TOPBAR_H               64
#define UI_ACTIONS_H              96
#define UI_SCREEN_PAD             UI_SPACE_4
#define UI_SCREEN_GAP             10

/* Stream-Slot (1fr) berechnet sich automatisch:
 *   stream_h = SCREEN_H - 2*PAD - TOPBAR_H - 2*GAP - ACTIONS_H
 *            = 1280 - 28 - 64 - 20 - 96 = 1072 */

/* Bell-Hero (Ringing-Screen).
 * S4-01 hatte auf 277 hochgesetzt fuer Pulse-Ring-Reserve (2.2x-Expansion).
 * S4-09: Pulse-Ringe + Glow weg (SW-Render zu teuer), Wrap braucht keinen
 * Effekt-Raum mehr. 200px = Goldilocks zwischen CSS-Default 160 und dem
 * S4-01-Bump 277 - gross genug fuers 800x1280-Display, schlank im Layout. */
#define UI_BELL_HERO_SIZE         200
#define UI_BELL_HERO_ICON         88     /* Lucide-88 Font-Groesse */

/* Ring-Buttons (Ignorieren/Tuer/Annehmen) - 68 Web-Mobile * 2.1 ~= 143 (S4-01). */
#define UI_RING_BTN_SIZE          143
#define UI_RING_BTN_ICON          64

/* S5-16/S5-18 Plan B: Klingel-UI als obere Status-Bar + untere Toolbar.
 *
 * Im Klingel-Modus liegen UI-Elemente AUSSERHALB der Stream-Region:
 * oben eine Status-Bar (88 px), unten eine Button-Toolbar (140 px),
 * Stream nur dazwischen. Beide UI-Bereiche reines LVGL im sicheren
 * Bereich - kein Doppel-Render-Konflikt mit Stream (der PPA-Overlay-
 * Pfad hat in S5-08..S5-15 wegen LVGL-Doppel-Render gescheitert).
 *
 *   Klingel-Header:           y =    0 ..   88  (UI_KLINGEL_HEADER_H = 88)
 *   Stream (Klingel-Video):   y =   88 .. 1135  (1047 hoch, Vollbreite)
 *   Klingel-Toolbar:          y = 1135 .. 1280  (UI_KLINGEL_TOOLBAR_H = 145)
 *
 * S5-18 Apple-Style:
 * - Status oben in der Header-Bar (gross, im Stil der Idle-Topbar)
 * - Untere Toolbar: Mittelgruppe (Annehmen/Tuer/Ablehnen) eng zusammen,
 *   Ignorieren + Record weit aussen abgesetzt
 * - Flach + opak, viel Schwarz, feine Hairlines, dezente Akzente
 * - KEINE Animation (Direct-FB-Perf, S5-17 bewiesen)
 *
 * S5-20/S5-21: Toolbar von 140 -> 150 (S5-20, +10) -> 145 (S5-21, +5 netto).
 * Inhalt sitzt per Flex-END unten-buendig, die 5 px Mehrhoehe wirken als
 * dezenter visueller Atem ueber der Button-Reihe. Stream-Video verliert
 * die 5 px nach unten (1052 -> 1047), Header bleibt.
 */
#define UI_KLINGEL_HEADER_H       88    /* Obere Status-Bar */
#define UI_KLINGEL_TOOLBAR_H      145   /* Untere Button-Bar (S5-20:+10, S5-21:-5) */
#define UI_KLINGEL_BTN_LG         96    /* Tuer (gross, primary) */
#define UI_KLINGEL_BTN_MD         72    /* Annehmen + Ablehnen */
#define UI_KLINGEL_BTN_SM         56    /* Ignorieren + Record */

/* Action-Buttons (Idle bottom bar) */
#define UI_ACTION_BTN_SIZE        52
#define UI_ACTION_BTN_ICON        22
#define UI_ACTION_GAP             26

/* Control-Group (Idle topbar center) */
#define UI_CTRL_BTN_SIZE          44
#define UI_CTRL_ICON              22
#define UI_CTRL_SEP_H             24


/* ============================================================
 * SHADOW EQUIVALENTS
 * ------------------------------------------------------------
 * LVGL Shadows haben width, offset, opacity, spread.
 * Wir uebersetzen die CSS-Shadows nahe.
 *
 * Web:   0 6px 14px -6px rgba(0,0,0,0.55)     (--shadow-md)
 * LVGL:  width=14, ofs_y=6, opa=140, spread=-6
 * ============================================================ */

#define UI_SHADOW_SM_WIDTH        4
#define UI_SHADOW_SM_OFS_Y        2
#define UI_SHADOW_SM_OPA          ((lv_opa_t)64)   /* 0.25 */

#define UI_SHADOW_MD_WIDTH        14
#define UI_SHADOW_MD_OFS_Y        6
#define UI_SHADOW_MD_OPA          ((lv_opa_t)140)  /* 0.55 */

#define UI_SHADOW_LG_WIDTH        40
#define UI_SHADOW_LG_OFS_Y        20
#define UI_SHADOW_LG_OPA          ((lv_opa_t)140)


/* ============================================================
 * COMPUTED PRESETS - oft genutzte Kombinationen
 * ------------------------------------------------------------
 * Diese koennen Helper-Funktionen in ui_tokens.c werden wenn
 * wir sie haeufiger brauchen. Vorerst nur Konstanten.
 * ============================================================ */

/* Action-Button-Primary-Style (Tuer-Auf im Idle-Screen)
 * = blauer Gradient + Glow-Shadow.
 * In LVGL erreichen wir das ueber:
 *   bg_color = UI_COLOR_ACCENT_LIGHT (top)
 *   bg_grad_color = UI_COLOR_ACCENT (bottom)
 *   bg_grad_dir = LV_GRAD_DIR_VER
 *   border_color = lighter accent
 *   shadow_color = UI_COLOR_ACCENT + UI_OPA_ACCENT_GLOW */

/* Ring-Btn-Styles fuer Ignorieren/Tuer/Annehmen:
 *   is-danger:  bg = UI_COLOR_DANGER, shadow = danger-glow
 *   is-warn:    bg = UI_COLOR_WARN,   shadow = warn-glow
 *   is-ok:      bg = UI_COLOR_OK,     shadow = ok-glow */


#ifdef __cplusplus
}
#endif
