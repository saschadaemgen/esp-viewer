/*
 * scr_idle.h - Idle screen, 1:1 from library/intercom-idle.html
 *
 * Layout (matches CSS .screen-idle grid-template-rows: 64px 1fr 96px):
 *
 *   topbar 64px   identity | control-group | clock
 *   stream 1fr    radius 22px, dark, stream canvas embedded here
 *   actions 96px  Mic | Tuer-auf (primary) | Verlauf
 *
 * Template server data:
 *   unit_name  = "Daemgen"
 *   door_name  = "Hauseingang"
 *   now        = "14:23:01"
 *   now_date   = "Di, 13. Mai"
 *   dnd        = false
 *   has_unread = false
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *unit_name;
    const char *door_name;
    const char *now;
    const char *now_date;
    bool dnd;
    bool has_unread;
} scr_idle_data_t;

/**
 * Build idle screen on the given lv_screen.
 *
 * Returns the .stream slot (lv_obj_t*) so the MJPEG pipeline
 * can render its canvas inside.
 */
lv_obj_t *scr_idle_build(lv_obj_t *screen, const scr_idle_data_t *data);

/**
 * Update the clock-time label.
 * Pass the screen returned/used in scr_idle_build.
 */
void scr_idle_set_time(lv_obj_t *screen, const char *now);

/**
 * Topbar-Uhrzeit setzen ("HH:MM:SS"). Update kommt aus dem
 * 1s-Timer der gegen time_sync_format_time_long fuettert. NULL wird
 * ignoriert. Safe to call any time after scr_idle_build.
 */
void scr_idle_set_clock_time(const char *txt);

/**
 * Topbar-Datum setzen (kompakt "MO, 18. MAI" / "MON, MAY 18").
 * Wird vom Topbar-Tick aus time_sync_format_date_short gefuettert.
 * NULL wird ignoriert.
 */
void scr_idle_set_clock_date(const char *txt);

/**
 * Update the unit-name label in the topbar (Mieter-Name).
 * Safe to call after scr_idle_build. NULL is ignored.
 */
void scr_idle_set_unit_name(const char *unit_name);

/**
 * Update the stream-meta label "cam  <door_name>" in the bottom-left
 * of the stream slot. Safe to call after scr_idle_build. NULL is ignored.
 */
void scr_idle_set_door_name(const char *door_name);

/**
 * Returns the modes-container (1fr slot between topbar and actions).
 * The container holds the stream-view child and accepts a settings-view
 * child via scr_idle_register_settings_view().
 *
 * Caller passes this to scr_settings_build(modes_container, ...).
 *
 * @return  modes container, or NULL if scr_idle_build was not called yet.
 */
lv_obj_t *scr_idle_get_modes_container(void);

/**
 * Registers the settings-view (as built by scr_settings_build) so that
 * scr_idle can animate between stream and settings modes.
 *
 * The view should be a direct child of get_modes_container(). The view
 * is positioned offscreen (parked below) and hidden until shown.
 *
 * @param settings_view  Root obj of the settings UI (from scr_settings_build)
 */
void scr_idle_register_settings_view(lv_obj_t *settings_view);

/**
 * Registers the screensaver-view (as built by scr_screensaver_build).
 *
 * The view should be a direct child of get_modes_container() and
 * starts in hidden state. Mode-switching between Stream and
 * Screensaver is a simple show/hide toggle (no animation), unlike
 * Settings which slides over.
 */
void scr_idle_register_screensaver_view(lv_obj_t *screensaver_view);

/**
 * Mode-switch: Stream sichtbar, Screensaver versteckt.
 * Idempotent.
 */
void scr_idle_show_stream_mode(void);

/**
 * Mode-switch: Screensaver sichtbar, Stream versteckt.
 * No-op wenn scr_idle_register_screensaver_view nicht gerufen wurde.
 */
void scr_idle_show_screensaver_mode(void);

/**
 * Togglet zwischen Stream und Screensaver Mode.
 * Verwendet vom Touch-Handler auf der Stream/Screensaver-Flaeche.
 * Settings-Idle-View-Mode bleibt unveraendert (das ist nur ein
 * temporaerer visueller Toggle).
 */
void scr_idle_toggle_idle_mode(void);

/**
 * @return true  wenn aktuell der Bildschirmschoner-View sichtbar ist
 * @return false wenn Stream-View sichtbar ist
 *
 * Verwendet vom idle_mode_mgr_doorbell_start um den vorherigen Modus
 * zu merken (S4-01). Settings-Overlay zaehlt nicht in dieser Abfrage -
 * Settings ist ein separater Overlay-State.
 */
bool scr_idle_is_screensaver_mode(void);

/**
 * Animates from stream-mode to settings-mode (slide-up, 400ms overshoot).
 * Idempotent: no-op if settings is already shown.
 * No-op if scr_idle_register_settings_view was not called yet.
 */
void scr_idle_show_settings(void);

/**
 * Animates from settings-mode back to stream-mode (slide-down).
 */
void scr_idle_show_stream(void);

/**
 * @return true if the settings view is currently shown (or sliding in)
 */
bool scr_idle_is_settings_shown(void);

/**
 * Toggle: wenn Settings angezeigt wird, schliesse zurueck zum Stream.
 * Sonst oeffne Settings. Verwendet von der Settings-Icon-Click-
 * Handler-Verkettung im main.c.
 */
void scr_idle_toggle_settings(void);

/**
 * Attaches a click handler to the settings icon (right side of the
 * control-group in the topbar). Multiple handlers can be stacked.
 */
void scr_idle_set_settings_handler(lv_event_cb_t cb, void *user_data);

/**
 * Attaches a click handler to the Verlauf-Button (rechter Button in
 * der Action-Bar). Setter-Pattern wie beim Settings-Icon damit der
 * Toast-Handler in main.c lebt (wo toast.h schon includiert ist).
 */
void scr_idle_set_history_handler(lv_event_cb_t cb, void *user_data);

/**
 * Setzt den Unread-Count und togglet das Badge am Verlauf-Button.
 *   count == 0  -> Badge HIDDEN
 *   count >  0  -> Badge sichtbar (Punkt, S15 ggf. mit Pulse)
 *
 * Heute zeigt das Badge keinen Zaehler, nur Praesenz. Idempotent.
 */
void scr_idle_set_unread_count(int count);

/**
 * S5-04 Teil C: Action-Bar (3 Idle-Buttons Mic/Tuer/Verlauf) ein-/
 * ausblenden. Wird vom Klingel-Overlay genutzt um Platz fuer die
 * grossen Klingel-Buttons (Ignorieren/Tuer/Annehmen) zu machen.
 *
 *   visible=false -> Action-Bar HIDDEN (Klingel-Buttons werden sichtbar)
 *   visible=true  -> Action-Bar sichtbar (Klingel ist vorbei)
 *
 * Idempotent. No-op wenn scr_idle_build noch nicht gelaufen ist.
 */
void scr_idle_set_actions_visible(bool visible);

/**
 * S5-11: Topbar (Identity/Control-Group/Clock, y=14..78) ein-/ausblenden.
 *
 * Im Klingel-Vollbild-Modus ist die Topbar weg - Stream geht y=0..1280
 * und wuerde Topbar uebermalen + LVGL flickert sie sekuendlich (Uhr).
 *
 * Idempotent. No-op wenn scr_idle_build noch nicht gelaufen ist.
 */
void scr_idle_set_topbar_visible(bool visible);

/**
 * S5-11: Design-Frame-Overlay (lv_layer_top, 14px LEFT/RIGHT-Border +
 * runde Ecken ueber der Stream-Region) ein-/ausblenden.
 *
 * Im Klingel-Vollbild-Modus soll Video bis an die Display-Raender -
 * keine Anthrazit-Streifen. Idempotent.
 */
void scr_idle_set_design_frame_visible(bool visible);

#ifdef __cplusplus
}
#endif
