/*
 * unifix_client.h - HTTP-Client fuer unifix-Server-API
 *
 * ESP-Saison 2 Tag 2
 *
 * Kapselt den HTTP-Zugriff auf die /esp/-API des unifix-Servers.
 * Authentifizierung via Bearer-Token (aus NVS via device_token.h).
 *
 * Server-Endpoint:
 *   http://192.168.1.42:9080/esp/...
 *
 * Saison 2 Spaeter:
 *   - HTTPS mit Cert-Pinning (Saison 17+)
 *   - SSE-Listener fuer /esp/events (separates Modul sse_client.c)
 *   - Server-URL aus NVS statt hardcoded (wenn Discovery kommt)
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialisiert den unifix-Client.
 *
 * Laedt den Bearer-Token aus NVS.
 *
 * @return ESP_OK              Bereit fuer API-Calls
 * @return ESP_ERR_NOT_FOUND   Kein Token in NVS
 * @return sonst              NVS-Fehler
 */
esp_err_t unifix_client_init(void);

/**
 * GET /esp/heartbeat
 *
 * Sendet einen Heartbeat-Request an den Server.
 * Erwartet Response: {"ok":true,"server_time":<unix_sec>}
 *
 * @return ESP_OK              Heartbeat erfolgreich, Server lebt
 * @return ESP_ERR_INVALID_STATE Client nicht initialisiert
 * @return ESP_ERR_INVALID_RESPONSE Token abgelehnt (HTTP 401) oder
 *                             Server-Fehler
 * @return ESP_FAIL            Netzwerk-Fehler
 */
esp_err_t unifix_client_heartbeat(void);

#ifdef __cplusplus
}
#endif
