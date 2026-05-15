/*
 * wifi_config.h - WLAN-SSID + Passwort-Storage in NVS
 *
 * ESP-Saison 2
 *
 * Speichert WLAN-Credentials persistent in NVS, damit das Geraet
 * sich beim Boot automatisch ins richtige WLAN einklinkt. Ersetzt
 * die hardcoded WIFI_SSID/WIFI_PASSWORD Defines aus main.c.
 *
 * NVS-Namespace: "unifix" (gleicher wie device_token)
 * NVS-Keys:      "wifi_ssid"  (max 32 Bytes incl NUL)
 *                "wifi_pwd"   (max 64 Bytes incl NUL)
 *
 * Sicherheit:
 *   Das Passwort ist SENSITIV. Nicht in Logs. Nicht in Commits.
 *   Nicht im Display anzeigen.
 *
 * Saison-Praxis:
 *   provision-esp.py --ssid SONCLOUD --password MK... schreibt
 *   beide Werte zusammen mit dem device_token in einem Rutsch.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID_MAX_LEN  32
#define WIFI_PWD_MAX_LEN   64

/**
 * Liest die SSID aus NVS.
 *
 * @return ESP_OK              SSID gelesen, in buf
 * @return ESP_ERR_NOT_FOUND   Keine SSID in NVS (Setup-Modus noetig)
 * @return ESP_ERR_INVALID_SIZE Buffer zu klein
 * @return sonst              NVS-Fehler
 */
esp_err_t wifi_config_get_ssid(char *buf, size_t buf_size);

/**
 * Liest das Passwort aus NVS.
 *
 * @return ESP_OK              Passwort gelesen, in buf
 * @return ESP_ERR_NOT_FOUND   Kein Passwort in NVS
 * @return ESP_ERR_INVALID_SIZE Buffer zu klein
 * @return sonst              NVS-Fehler
 */
esp_err_t wifi_config_get_password(char *buf, size_t buf_size);

/**
 * Speichert SSID und Passwort in NVS (in einem Commit).
 *
 * @param ssid     Nul-terminierter SSID-String
 * @param password Nul-terminierter Passwort-String (darf "" sein fuer offene Netze)
 *
 * @return ESP_OK              Erfolgreich gespeichert
 * @return ESP_ERR_INVALID_ARG ssid NULL/leer
 * @return ESP_ERR_INVALID_SIZE ssid oder password zu lang
 * @return sonst              NVS-Fehler
 */
esp_err_t wifi_config_set(const char *ssid, const char *password);

/**
 * Loescht WLAN-Credentials aus NVS.
 *
 * @return ESP_OK   Erfolgreich (auch wenn vorher leer)
 * @return sonst   NVS-Fehler
 */
esp_err_t wifi_config_clear(void);

/**
 * Prueft ob WLAN-Credentials vorhanden sind.
 *
 * @return true   SSID und Passwort vorhanden
 * @return false  Mindestens eines fehlt (Setup-Modus noetig)
 */
bool wifi_config_has(void);

#ifdef __cplusplus
}
#endif
