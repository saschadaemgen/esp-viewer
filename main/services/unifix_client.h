/*
 * unifix_client.h - HTTP-Client fuer unifix-Server-API
 *
 * Kapselt den HTTP-Zugriff auf die /esp/-API des unifix-Servers.
 * Authentifizierung via Bearer-Token (aus NVS via device_token.h).
 *
 * Server-Endpoint:
 *   http://192.168.1.42:9080/esp/...
 *
 * Spaeter:
 *   - HTTPS mit Cert-Pinning
 *   - Server-URL aus NVS statt hardcoded (wenn Discovery kommt)
 *   - POST /esp/answer
 *   - POST /esp/unlock
 *   - GET  /esp/config
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

/**
 * POST /esp/reject
 *
 * Sendet eine Ablehnung an den unifix-Server fuer das gegebene Event.
 * Der Server triggert daraufhin den /call_admin_result-RPC zum UDM,
 * welcher die UA-Intercom-Hardware verstummen laesst.
 *
 * Body: {"event_id":"<cancel_token>"}
 *
 * @param event_id  Der cancel_token aus dem SSE doorbell.ring Event
 *
 * @return ESP_OK                   Server hat den Reject akzeptiert (HTTP 2xx)
 * @return ESP_ERR_INVALID_STATE    Client nicht initialisiert
 * @return ESP_ERR_INVALID_ARG      event_id NULL, leer oder zu lang
 * @return ESP_ERR_INVALID_RESPONSE Server hat mit Fehler-Status geantwortet
 * @return ESP_FAIL                 Netzwerk-Fehler
 */
esp_err_t unifix_client_reject(const char *event_id);

#ifdef __cplusplus
}
#endif
