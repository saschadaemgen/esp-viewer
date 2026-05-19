/*
 * scr_settings.h - Settings-Screen (Auto-Save-Pattern, 5 Sektionen)
 *
 * Erscheint als absolute-positioniertes Overlay anstelle des Stream-
 * Slots im scr_idle (modes-container). Topbar und Actions-Row bleiben
 * sichtbar.
 *
 * Sektionen (Master-Chat-Spec S14-XX + S03-09):
 *   1. Idle-Ansicht         (Radios: screensaver / livestream / screen_off)
 *   2. Auto-Bildschirmschoner (Radios: 0/30/60/300/600 Sek)
 *   3. Bildschirm aus nach    (Radios: 0/30/60/300/600/1800 Sek)
 *   4. Helligkeit             (Slider 0-100, Live-Preview, 300ms Debounce)
 *   5. Sprache                (Radios: Deutsch / English)
 *   6. Uhr-Anzeige            (Radios: Vertikal / Horizontal)
 *   + CARVILON-Footer
 *
 * Save-Pattern: Auto-Save
 *   - Jede Aenderung loest sofort den change_handler aus
 *   - Brightness-Slider: change_handler nach 300ms Debounce
 *   - Kein Speichern-Button, kein Abbrechen
 *   - X-Icon oben rechts schliesst den Screen (cancel_handler)
 *
 * Toast: nach jedem erfolgreichem change_handler-Returnwert ESP_OK
 *   blendet der Caller via toast_show("Gespeichert") einen kleinen
 *   Banner fuer 1.5s ein.
 */

#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "services/unifix_config.h"   /* idle_view_mode_t */
#include "services/time_sync.h"        /* language_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Initial values ---------------- */

typedef struct {
    idle_view_mode_t  initial_idle_view_mode;
    int               initial_auto_screensaver_sec;   /* 0/30/60/300/600 */
    int               initial_screen_off_sec;         /* 0/30/60/300/600/1800 */
    int               initial_brightness;             /* 0..100 */
    language_t        initial_language;
    clock_layout_t    initial_clock_layout;            /* S03-09 */
} scr_settings_data_t;

/* ---------------- Change snapshot ---------------- */

/**
 * Wird bei jeder Aenderung an den change_handler uebergeben. Enthaelt
 * den KOMPLETTEN aktuellen Settings-State, nicht nur das geaenderte
 * Feld. Caller schickt das als POST /esp/settings.
 */
typedef struct {
    idle_view_mode_t  idle_view_mode;
    int               auto_screensaver_sec;
    int               screen_off_sec;
    int               brightness;
    language_t        language;
    clock_layout_t    clock_layout;                    /* S03-09 */
} scr_settings_snapshot_t;

/* ---------------- Callbacks ---------------- */

/**
 * Live-Brightness-Preview-Callback - sofortig bei jeder Slider-Bewegung
 * (ohne Debounce). Caller verbindet das normalerweise mit dem Backlight-
 * Manager fuer sofortige optische Reaktion.
 */
typedef void (*scr_settings_brightness_preview_cb_t)(int brightness_percent,
                                                     void *user_data);

/**
 * Change-Handler - wird bei JEDER Aenderung gerufen, nach erfolgreicher
 * lokaler Anwendung. Bei Brightness erst nach 300ms-Debounce.
 *
 * Caller verbindet das mit unifix_client_post_settings(). Bei Erfolg
 * sollte der Caller toast_show("Gespeichert") triggern.
 */
typedef void (*scr_settings_change_cb_t)(const scr_settings_snapshot_t *snap,
                                          void *user_data);

/* ---------------- Build ---------------- */

/**
 * Erzeugt den Settings-Screen als absoluten Container im parent.
 * Der Container fuellt parent komplett aus.
 * Initial visible: caller setzt LV_OBJ_FLAG_HIDDEN nach Bedarf.
 *
 * @return root-Container fuer Show/Hide
 */
lv_obj_t *scr_settings_build(lv_obj_t *parent, const scr_settings_data_t *data);

/* ---------------- External value updates (from config.changed) ---------------- */

/**
 * Aktualisiert die UI-Anzeige mit neuen Werten vom Server, z.B. wenn
 * ein anderes Geraet des selben Mieters Settings geaendert hat
 * (config.changed SSE-Event). Triggert KEINEN change_handler.
 */
void scr_settings_apply_external(const scr_settings_snapshot_t *snap);

/* ---------------- Callbacks registration ---------------- */

void scr_settings_set_brightness_preview_cb(scr_settings_brightness_preview_cb_t cb,
                                            void *user_data);

void scr_settings_set_change_handler(scr_settings_change_cb_t cb,
                                      void *user_data);

/**
 * Click-Handler fuer X-Icon oben rechts (schliesst den Screen).
 * Caller blendet typischerweise zurueck zum Idle-Default-Mode.
 */
void scr_settings_set_close_handler(lv_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
