/*
 * idle_mode_mgr.h - Idle-Mode + Backlight Manager (Schritt 7 Refactor)
 *
 * Ersetzt den frueheren screensaver.c. Verantwortlich fuer drei klare
 * Pfade je nach idle_view_mode (siehe Master-Chat-Spec vom 17. Mai 2026):
 *
 *   Pfad 1: SCREENSAVER
 *     Backlight bleibt IMMER auf brightness_idle (kein Aus!).
 *     Bei Inaktivitaet > auto_screensaver_sec -> Mode-Switch zurueck
 *     zum Bildschirmschoner-View (falls gerade Stream sichtbar).
 *
 *   Pfad 2: LIVESTREAM
 *     Backlight bleibt IMMER auf brightness_idle.
 *     Bei Inaktivitaet > auto_screensaver_sec -> Mode-Switch zurueck
 *     zum Stream-View (falls gerade Bildschirmschoner sichtbar).
 *
 *   Pfad 3: SCREEN_OFF
 *     Backlight default brightness_idle.
 *     Nach screen_off_after_sec Inaktivitaet -> Backlight 0 (Display aus).
 *     Touch weckt auf, fade-in auf brightness_idle.
 *     Mode-Switch passiert hier NICHT - das Display ist eh aus.
 *
 * Doorbell-Override (alle Pfade):
 *   idle_mode_mgr_doorbell_start() -> Backlight 100% hart sofort
 *   idle_mode_mgr_doorbell_end()   -> Backlight zurueck auf brightness_idle
 *
 * Touch-Tracking:
 *   Default via lv_display_get_inactive_time. Plus explizite Resets
 *   via idle_mode_mgr_notify_activity() bei Wake-Pfaden.
 *
 * Thread-Safety:
 *   update_config / notify_activity / doorbell_* sind FreeRTOS-safe via
 *   interne Mutex. Mode-Switch-Aufrufe an scr_idle_show_*_mode passieren
 *   im LVGL-Poll-Timer-Context (lv_timer), brauchen also keinen lock.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "services/unifix_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BACKLIGHT_REASON_IDLE,        /* brightness_idle */
    BACKLIGHT_REASON_DOORBELL,    /* 100% temporaer */
    BACKLIGHT_REASON_OFF,         /* 0, nur in SCREEN_OFF-Mode nach Timeout */
} backlight_reason_t;

/**
 * Startet das Idle-Mode-Mgr-Modul.
 *
 * Erzeugt einen LVGL-Timer (500ms tick) der die Inaktivitaet pollt.
 * Initial-Backlight wird auf brightness_idle gesetzt.
 *
 * Muss EINMAL beim Boot nach bsp_display_start_with_config aufgerufen
 * werden. Idempotent - zweiter Aufruf gibt ESP_ERR_INVALID_STATE.
 *
 * @return ESP_OK                 Timer + State bereit
 * @return ESP_ERR_NO_MEM         Mutex- oder Timer-Erzeugung fehlgeschlagen
 * @return ESP_ERR_INVALID_STATE  Bereits gestartet
 */
esp_err_t idle_mode_mgr_start(void);

/**
 * Updated die Konfiguration. Idempotent - kann beliebig oft aufgerufen
 * werden (Boot-Apply, config.changed, on_settings_changed).
 *
 * Effekte:
 *   - Wenn Display im OFF-State ist und neuer Mode != SCREEN_OFF:
 *     Backlight wird auf brightness_idle aufgeweckt.
 *   - Wenn brightness_idle sich aendert UND aktuell nicht aus/klingel:
 *     Backlight wird auf den neuen Wert gesetzt.
 *   - Inaktivitaets-Timer wird NICHT zurueckgesetzt.
 *
 * @param idle_mode               IDLE_MODE_SCREENSAVER / LIVESTREAM / SCREEN_OFF
 * @param auto_screensaver_sec    Sekunden bis Mode-Auto-Switch (0 = nie)
 * @param screen_off_after_sec    Sekunden bis Backlight aus (0 = nie, nur SCREEN_OFF)
 * @param brightness_idle         0..100 Prozent
 */
void idle_mode_mgr_update_config(idle_view_mode_t idle_mode,
                                  int auto_screensaver_sec,
                                  int screen_off_after_sec,
                                  int brightness_idle);

/**
 * Aktivitaet signalisieren - resettet den Inaktivitaets-Timer und
 * weckt das Display falls es aus war.
 *
 * Zusaetzlich zum LVGL-Touch-Track. Wird intern bei doorbell_end und
 * von externen Stellen (z.B. config-changed durch User-Aktion) gerufen.
 */
void idle_mode_mgr_notify_activity(void);

/**
 * Klingel-Start: Backlight 100% hart sofort, ohne Fade.
 * Display wird aus dem OFF-State sofort aufgeweckt.
 * Setzt internen reason = DOORBELL bis doorbell_end gerufen wird.
 *
 * Idempotent.
 */
void idle_mode_mgr_doorbell_start(void);

/**
 * Klingel-Ende: Backlight zurueck auf brightness_idle.
 * Inaktivitaets-Timer wird zurueckgesetzt.
 *
 * Idempotent.
 */
void idle_mode_mgr_doorbell_end(void);

/**
 * Aktueller Backlight-Status fuer Debug-Logs.
 */
backlight_reason_t idle_mode_mgr_current_reason(void);

#ifdef __cplusplus
}
#endif
