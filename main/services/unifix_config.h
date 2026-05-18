/*
 * unifix_config.h - /esp/config Konsumption + In-Memory-State
 *
 * Holt die Geraete-Konfiguration vom CARVILON-Server, persistiert sie
 * im NVS-Cache und stellt sie zentralisiert als In-Memory-Struktur
 * zur Verfuegung. UI-Komponenten registrieren sich als Listener und
 * werden bei Aenderungen benachrichtigt.
 *
 * Fallback-Hierarchie pro Feld:
 *   1. Live-Response vom Server
 *   2. NVS-Cache (zuletzt erfolgreich geholt)
 *   3. Compile-Time-Default
 *
 * Thread-Safety:
 *   Der In-Memory-State ist Mutex-geschuetzt. unifix_config_get()
 *   kopiert die aktuelle Snapshot-Struktur in einen Caller-Buffer,
 *   damit der Caller nach Release der Mutex sicher arbeiten kann.
 *
 * Sequencing:
 *   Boot:
 *     unifix_config_init()           Mutex + State auf Defaults
 *     unifix_config_load_from_cache() NVS lesen, State updaten
 *     unifix_config_fetch_and_apply()  HTTP GET, Cache + State + Listener
 *
 *   SSE config.changed:
 *     unifix_config_fetch_and_apply()
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Idle-Mode enum ---------------- */

typedef enum {
    IDLE_MODE_SCREENSAVER = 0,   /* Stream-Slot zeigt Uhr/Datum/Wetter */
    IDLE_MODE_LIVESTREAM,        /* Stream-Slot zeigt Live-Kamera */
    IDLE_MODE_SCREEN_OFF,        /* Backlight auf 0 nach Inaktivitaet (ESP-only) */
} idle_view_mode_t;

/**
 * Wandelt einen idle_view_mode in seinen Server-String um
 * ("screensaver" | "livestream" | "screen_off").
 * Bei unbekanntem Enum-Wert: "screensaver".
 */
const char *unifix_config_mode_to_str(idle_view_mode_t mode);

/**
 * Parst einen Server-String in einen idle_view_mode.
 * Bei unbekanntem oder NULL-String: IDLE_MODE_SCREENSAVER.
 */
idle_view_mode_t unifix_config_mode_from_str(const char *s);

/* ---------------- Defaults (compile-time) ---------------- */

#define UNIFIX_CONFIG_DEFAULT_MIETER_NAME            ""
#define UNIFIX_CONFIG_DEFAULT_LOCATION_NAME          "Hauseingang"
#define UNIFIX_CONFIG_DEFAULT_LANGUAGE               "de"
#define UNIFIX_CONFIG_DEFAULT_BRIGHTNESS_IDLE        70
#define UNIFIX_CONFIG_DEFAULT_AUTO_SCREENSAVER_SEC   60
#define UNIFIX_CONFIG_DEFAULT_SCREEN_OFF_SEC         0   /* 0 = nie */
#define UNIFIX_CONFIG_DEFAULT_IDLE_VIEW_MODE         IDLE_MODE_SCREENSAVER

/* ---------------- Field-size limits (NUL-terminated) ---------------- */

#define UNIFIX_CONFIG_MIETER_NAME_MAX   64
#define UNIFIX_CONFIG_LOCATION_NAME_MAX 64
#define UNIFIX_CONFIG_LANGUAGE_MAX      8

/* ---------------- Snapshot struct ---------------- */

typedef struct {
    char             mieter_name[UNIFIX_CONFIG_MIETER_NAME_MAX];
    char             location_name[UNIFIX_CONFIG_LOCATION_NAME_MAX];
    char             language[UNIFIX_CONFIG_LANGUAGE_MAX];

    idle_view_mode_t idle_view_mode;
    int              auto_screensaver_seconds;   /* 0/30/60/300/600 */
    int              screen_off_after_sec;       /* 0/30/60/300/600/1800 */
    int              brightness_idle;            /* 0..100 percent */
} unifix_config_t;

/* ---------------- Listener pattern ---------------- */

/**
 * Callback-Signatur fuer Config-Aenderungen.
 *
 * Wird aufgerufen NACHDEM der In-Memory-State und der NVS-Cache
 * aktualisiert wurden. Der Callback bekommt den NEUEN Stand als
 * Kopie, kann ihn also ohne Mutex-Lock benutzen.
 *
 * Aufgerufen aus dem Worker-Task. Falls der Callback LVGL anfasst,
 * muss er selber bsp_display_lock nehmen.
 */
typedef void (*unifix_config_listener_t)(const unifix_config_t *cfg);

/* ---------------- Lifecycle ---------------- */

/**
 * Initialisiert das Modul. Setzt Mutex und State auf Defaults.
 * Muss EINMAL beim Boot vor allen anderen Calls aufgerufen werden.
 *
 * @return ESP_OK              Bereit
 * @return ESP_ERR_NO_MEM      Mutex-Erzeugung fehlgeschlagen
 */
esp_err_t unifix_config_init(void);

/* ---------------- Operations ---------------- */

/**
 * Liest den NVS-Cache (falls vorhanden), parsed das JSON und
 * uebernimmt die Werte in den In-Memory-State.
 *
 * Loest KEINE Listener aus (Boot-Pfad: Listener werden erst spaeter
 * registriert).
 *
 * @return ESP_OK              Cache geladen, State aktualisiert
 * @return ESP_ERR_NOT_FOUND   Kein Cache vorhanden, State bleibt auf Defaults
 * @return sonst               NVS- oder JSON-Parse-Fehler (State bleibt unveraendert)
 */
esp_err_t unifix_config_load_from_cache(void);

/**
 * Holt /esp/config vom Server, parsed das JSON, schreibt NVS-Cache,
 * aktualisiert In-Memory-State und ruft alle registrierten Listener.
 *
 * Verwendet den Bearer-Token aus device_token (NVS).
 *
 * @return ESP_OK                    Erfolgreich geholt und angewendet
 * @return ESP_ERR_INVALID_STATE     unifix_config_init wurde nicht aufgerufen
 * @return ESP_ERR_INVALID_RESPONSE  HTTP 4xx / 5xx vom Server
 * @return ESP_FAIL                  Netzwerk- oder Parse-Fehler
 */
esp_err_t unifix_config_fetch_and_apply(void);

/**
 * Kopiert den aktuellen In-Memory-State in den Caller-Buffer.
 * Thread-safe via interne Mutex.
 *
 * @param out_cfg  Caller-allocated Struct
 *
 * @return ESP_OK              Snapshot kopiert
 * @return ESP_ERR_INVALID_ARG out_cfg ist NULL
 * @return ESP_ERR_INVALID_STATE unifix_config_init wurde nicht aufgerufen
 */
esp_err_t unifix_config_get(unifix_config_t *out_cfg);

/* ---------------- Listener registration ---------------- */

/**
 * Registriert einen Callback fuer Config-Aenderungen.
 *
 * Maximal UNIFIX_CONFIG_MAX_LISTENERS koennen registriert werden.
 * Duplikate (gleicher Funktions-Pointer) werden abgelehnt.
 *
 * @param cb   Callback-Funktion
 *
 * @return ESP_OK              Registriert
 * @return ESP_ERR_INVALID_ARG cb ist NULL
 * @return ESP_ERR_NO_MEM      Listener-Tabelle voll
 * @return ESP_ERR_INVALID_STATE Callback bereits registriert
 */
esp_err_t unifix_config_register_listener(unifix_config_listener_t cb);

#ifdef __cplusplus
}
#endif
