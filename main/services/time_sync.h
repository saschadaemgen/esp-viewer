/*
 * time_sync.h - NTP-Sync + Locale-Helper fuer Uhrzeit und Datum
 *
 * Verantwortung:
 *   - SNTP-Client starten (pool.ntp.org Primary, kein Fallback)
 *   - Timezone setzen auf Europe/Berlin
 *   - Stateless Format-Helper fuer "HH:MM" und 
 *     "Wochentag, DD. Monat YYYY" / "Weekday, Month DD, YYYY"
 *
 * Lokalisierung:
 *   Deutsch: "Sonntag, 17. Mai 2026"   -> "%s, %d. %s %d"
 *   English: "Sunday, May 17, 2026"     -> "%s, %s %d, %d"
 *
 * Uhrzeit:
 *   24h-Format, KEINE Sekunden, KEINE AM/PM.
 *
 * Loading-State:
 *   Bis NTP synced:
 *     time_sync_format_time -> "--:--"
 *     time_sync_format_date -> ""
 *     time_sync_is_synced   -> false
 *
 * Caller-Verantwortung:
 *   Sprach-Parameter wird vom Caller anhand unifix_config.language
 *   uebergeben. Modul selbst kennt keinen Config-Listener (stateless).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LANG_DE = 0,
    LANG_EN,
} language_t;

/**
 * Wandelt einen Sprach-String ("de" | "en" | ...) in einen language_t um.
 * Unbekannt oder NULL -> LANG_DE.
 */
language_t time_sync_lang_from_str(const char *s);

/**
 * Startet den SNTP-Client gegen pool.ntp.org und setzt die
 * System-Timezone auf Europe/Berlin (CET-1CEST,M3.5.0,M10.5.0/3).
 *
 * Asynchron - die erste Synchronisation dauert ueblicherweise
 * 1-5 Sekunden nach dem WLAN-Connect. Caller pollt
 * time_sync_is_synced() oder wartet auf time_sync_wait_for_sync().
 *
 * Idempotent: zweiter Aufruf macht nichts.
 *
 * @return ESP_OK              SNTP-Client gestartet
 * @return ESP_ERR_INVALID_STATE Bereits gestartet
 */
esp_err_t time_sync_start(void);

/**
 * Blockierende Variante: wartet bis NTP synced ist oder das Timeout
 * abgelaufen ist. Hilfreich beim Boot um zu vermeiden dass der
 * Bildschirmschoner mit "--:--" startet.
 *
 * @param timeout_ms  Maximale Wartezeit in Millisekunden
 *
 * @return true  Synced innerhalb des Timeouts
 * @return false Timeout abgelaufen, immer noch nicht synced
 */
bool time_sync_wait_for_sync(uint32_t timeout_ms);

/**
 * @return true wenn die System-Zeit per NTP synchronisiert wurde
 *         und damit valide ist.
 */
bool time_sync_is_synced(void);

/**
 * Schreibt die aktuelle Uhrzeit als "HH:MM" in den Buffer.
 * 24h-Format, keine Sekunden.
 *
 * Falls noch nicht synced: schreibt "--:--".
 *
 * @param buf     Output-Buffer
 * @param buflen  Buffer-Groesse (min. 6 Bytes)
 * @param lang    Sprache - heute nur fuers Format-Symbol relevant
 *                (24h in beiden Sprachen identisch). Reserviert
 *                fuer spaetere AM/PM-Option.
 */
void time_sync_format_time(char *buf, size_t buflen, language_t lang);

/**
 * Schreibt das aktuelle Datum lokalisiert in den Buffer.
 *
 * Deutsch:  "Sonntag, 17. Mai 2026"
 * English:  "Sunday, May 17, 2026"
 *
 * Falls noch nicht synced: schreibt einen leeren String.
 *
 * @param buf     Output-Buffer
 * @param buflen  Buffer-Groesse (min. 48 Bytes fuer lange Wochentage
 *                wie "Mittwoch" + "September" + Jahr)
 * @param lang    Sprache
 */
void time_sync_format_date(char *buf, size_t buflen, language_t lang);

/**
 * Kompakte Form fuer die Topbar (Uhrzeit-Block oben rechts).
 *
 * Deutsch:  "SO, 17. MAI 2026"   (Wochentag-Kuerzel 2 Buchstaben + Monat-3-letter)
 * English:  "SUN, MAY 17, 2026"  (Wochentag-Kuerzel 3 Buchstaben)
 *
 * Falls noch nicht synced: schreibt einen leeren String.
 *
 * @param buf     Output-Buffer
 * @param buflen  Buffer-Groesse (min. 24 Bytes)
 * @param lang    Sprache
 */
void time_sync_format_date_short(char *buf, size_t buflen, language_t lang);

#ifdef __cplusplus
}
#endif
