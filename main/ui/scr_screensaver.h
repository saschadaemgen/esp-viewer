/*
 * scr_screensaver.h - Bildschirmschoner-View (Uhr/Datum/Wetter/Badge)
 *
 * Wird vom scr_idle als Kind des modes-containers angelegt, neben
 * stream_view und settings_view. Master-Chat-Spec S14-XX: 1:1-Klon
 * des Web-Viewer-Bildschirmschoners.
 *
 * Inhalt:
 *   Uhrzeit (Montserrat-200, "HH:MM")
 *   Datum (Montserrat-18, "Wochentag, DD. Monat YYYY" lokalisiert)
 *   Wetter-Zeile (Lucide-22-Icon + Temperatur + Beschreibung)
 *   Verpasst-Badge (pill, pulse-Animation, nur wenn count > 0)
 *
 * Pulse-Animation: opacity 0.7 -> 1.0, 2400ms ease-in-out alternate
 *
 * Update-Trigger:
 *   Uhrzeit + Datum: 1-Sekunden-Timer im scr_screensaver
 *   Sprache:         per scr_screensaver_set_language(lang)
 *   Wetter:          per scr_screensaver_set_weather(...)
 *   Badge:           per scr_screensaver_set_unread_count(count)
 *
 * Caller (main.c) ist verantwortlich fuer:
 *   - Initial-Call und periodische Refresh der Wetter-Daten
 *   - Update der Sprache wenn config.changed kommt
 *   - Update des Badges bei SSE unread_count Event
 */

#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "services/time_sync.h"
#include "services/unifix_config.h"   /* clock_layout_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Erzeugt den Bildschirmschoner-View als absolut positioniertes
 * Kind im parent (modes-container des scr_idle). Fuellt parent
 * komplett aus (lv_pct(100) x lv_pct(100)).
 *
 * Initial sichtbar - der Caller setzt LV_OBJ_FLAG_HIDDEN je nach
 * aktuellem idle_view_mode.
 *
 * Startet intern einen 1-Sekunden-LVGL-Timer der Uhrzeit + Datum
 * aktualisiert.
 *
 * @param parent Eltern-Container
 * @param lang   Initial-Sprache fuer Datums-Lokalisierung
 *
 * @return root-Container fuer show/hide
 */
lv_obj_t *scr_screensaver_build(lv_obj_t *parent, language_t lang);

/**
 * Aendert die Sprache fuer Datums-Lokalisierung.
 * Triggert sofort einen Datums-Refresh.
 */
void scr_screensaver_set_language(language_t lang);

/**
 * Schaltet zwischen den beiden Uhr-Layouts um. Beide UI-Baeume werden
 * beim Build erzeugt; dieser Setter togglet nur die Visibility -
 * kein Flackern, kein Re-Build.
 *
 * VERTICAL    Pixel-Style: zwei montserrat_200-Labels untereinander
 *             (default, vorhandenes Layout).
 * HORIZONTAL  Klassisch: ein montserrat_140-Label "HH:MM" in einer
 *             Zeile.
 *
 * Idempotent.
 */
void scr_screensaver_set_clock_layout(clock_layout_t layout);

/**
 * Setzt die Wetter-Anzeige. Caller laedt die Daten von
 * /esp/weather und ruft das hier auf.
 *
 * @param temp_c           Temperatur in Celsius
 * @param condition_text   "Bewölkt", "Sonnig", ...
 * @param icon_code        Lucide-Icon-Name ("cloud", "sun", ...)
 */
void scr_screensaver_set_weather(int temp_c,
                                  const char *condition_text,
                                  const char *icon_code);

/**
 * Setzt den Verpasst-Count. count <= 0 -> Badge wird versteckt.
 * count > 0  -> Badge wird angezeigt mit "N verpasst" (de) oder
 *               "N missed" (en).
 */
void scr_screensaver_set_unread_count(int count);

#ifdef __cplusplus
}
#endif
