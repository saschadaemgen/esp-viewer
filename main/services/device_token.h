/*
 * device_token.h - Bearer-Token-Storage in NVS
 *
 * ESP-Saison 2 Tag 2
 *
 * Speichert den Bearer-Token fuer die unifix-Server-API
 * persistent in NVS (Non-Volatile Storage).
 *
 * NVS-Namespace: "unifix"
 * NVS-Key:       "device_token"
 * Maximalgroesse: 128 Bytes (Token + NUL)
 *
 * Sicherheit:
 *   Der Token ist SENSITIV. Nicht in Logs ausgeben. Nicht in
 *   Commits. Nicht im Display anzeigen ausser im Setup-Modus.
 *   Falls Token leakt: Master-Chat regeneriert.
 *
 * Saison 2 Spaeter:
 *   - NVS-Encryption-Backend nutzen falls verfuegbar
 *   - Token-Rotation via auth.token.rotate SSE-Event
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_TOKEN_MAX_LEN 128

/**
 * Liest den Token aus NVS.
 *
 * @param buf       Ziel-Buffer, mindestens DEVICE_TOKEN_MAX_LEN gross
 * @param buf_size  Groesse des Buffers (typisch DEVICE_TOKEN_MAX_LEN)
 *
 * @return ESP_OK              Token gelesen, in buf
 * @return ESP_ERR_NOT_FOUND   Kein Token in NVS (Setup-Modus aktivieren!)
 * @return ESP_ERR_INVALID_SIZE Buffer zu klein
 * @return sonst              NVS-Fehler
 */
esp_err_t device_token_get(char *buf, size_t buf_size);

/**
 * Speichert einen Token in NVS.
 *
 * Ueberschreibt eventuell vorhandenen Token.
 * Token muss nul-terminiert sein, Laenge unter DEVICE_TOKEN_MAX_LEN.
 *
 * @param token     Nul-terminierter Token-String
 *
 * @return ESP_OK              Erfolgreich gespeichert
 * @return ESP_ERR_INVALID_ARG token NULL oder leer
 * @return ESP_ERR_INVALID_SIZE Token zu lang
 * @return sonst              NVS-Fehler
 */
esp_err_t device_token_set(const char *token);

/**
 * Loescht den Token aus NVS.
 *
 * Naechster Boot landet im Setup-Modus.
 *
 * @return ESP_OK              Erfolgreich geloescht (auch wenn vorher leer)
 * @return sonst              NVS-Fehler
 */
esp_err_t device_token_clear(void);

/**
 * Prueft ob ein Token vorhanden ist, ohne ihn zu lesen.
 *
 * @return true   Token vorhanden
 * @return false  Kein Token (Setup-Modus noetig)
 */
bool device_token_has(void);

#ifdef __cplusplus
}
#endif
