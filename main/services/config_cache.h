/*
 * config_cache.h - NVS-Cache fuer /esp/config JSON-Snapshot
 *
 * Speichert die zuletzt erfolgreich vom Server abgerufene Config
 * als JSON-String in NVS, damit das Geraet auch ohne Server-Verbindung
 * mit der zuletzt bekannten Konfiguration arbeiten kann.
 *
 * Namespace: "unifix" (geteilt mit device_token + wifi_config)
 * Key:       "config_cache"
 * Wert:      JSON-String, max CONFIG_CACHE_MAX_LEN Bytes
 *
 * Fallback-Hierarchie fuer Config-Werte:
 *   1. Server-Response (live)
 *   2. NVS-Cache (zuletzt bekannt)
 *   3. Compile-Time-Default (in unifix_config.c)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum size of cached JSON in bytes (incl. terminating NUL).
 * Heutige Config: ~300 Bytes. Geplanter Vollausbau mit
 * doors[] + cameras[]: ~2 KB. 4 KB ist konservativ-grosszuegig. */
#define CONFIG_CACHE_MAX_LEN  4096

/**
 * Schreibt einen JSON-String in den NVS-Cache.
 *
 * @param json_str  Nul-terminierter JSON-String, max CONFIG_CACHE_MAX_LEN-1 Bytes
 *
 * @return ESP_OK                    Erfolgreich geschrieben
 * @return ESP_ERR_INVALID_ARG       json_str NULL oder zu lang
 * @return sonst                     NVS-Fehler (Flash voll, etc.)
 */
esp_err_t config_cache_save(const char *json_str);

/**
 * Liest den zuletzt gespeicherten JSON-String aus dem NVS-Cache.
 *
 * @param out_buf       Output-Buffer fuer den JSON-String (caller-allocated)
 * @param buf_size      Groesse des Output-Buffers in Bytes
 *
 * @return ESP_OK                    String erfolgreich geladen, nul-terminiert
 * @return ESP_ERR_NOT_FOUND         Kein Cache-Eintrag vorhanden (Initial-State)
 * @return ESP_ERR_INVALID_ARG       out_buf NULL oder buf_size 0
 * @return ESP_ERR_INVALID_SIZE      Buffer zu klein fuer den gespeicherten Wert
 * @return sonst                     NVS-Fehler
 */
esp_err_t config_cache_load(char *out_buf, size_t buf_size);

/**
 * Loescht den Cache-Eintrag (z.B. bei Factory-Reset oder Token-Rotation).
 *
 * @return ESP_OK                    Erfolgreich geloescht oder war bereits leer
 * @return sonst                     NVS-Fehler
 */
esp_err_t config_cache_clear(void);

/**
 * Schnell-Check ob ein Cache-Eintrag existiert.
 *
 * @return true   Eintrag vorhanden
 * @return false  Kein Eintrag oder NVS-Fehler
 */
bool config_cache_has(void);

#ifdef __cplusplus
}
#endif
