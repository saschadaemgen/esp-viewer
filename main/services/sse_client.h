/*
 * sse_client.h - Server-Sent-Events Listener fuer /esp/events
 *
 * ESP-Saison 2 Tag 3
 *
 * Verbindet sich mit dem unifix-Server auf einen SSE-Endpoint
 * und ruft fuer jedes empfangene Event den Callback auf.
 *
 * SSE-Frame-Format (RFC: text/event-stream):
 *   event: <name>
 *   data: <payload>
 *   <leerzeile>
 *
 * Architektur:
 *   - Eigene FreeRTOS-Task ('sse_listener')
 *   - Verwendet esp_http_client_open/read (Low-Level Long-Stream)
 *   - Exponential Backoff bei Connection-Loss: 1s, 2s, 4s, 8s, max 30s
 *   - Bearer-Auth via Authorization-Header aus device_token
 *
 * Saison 2 Spaeter:
 *   - Bei doorbell_start -> Event-Queue an FSM
 *   - Bei config.changed -> erneutes unifix_client_config laden
 *   - Bei auth.token.rotate -> Token in NVS updaten
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Callback-Signatur fuer empfangene SSE-Events.
 *
 * @param event_name  Name des Events (z.B. "heartbeat", "doorbell_start")
 * @param data        Daten-Payload (JSON-String), oder leerer String
 *
 * Beide Strings sind nul-terminiert. Sie sind nur waehrend des
 * Callbacks gueltig - bei Bedarf kopieren.
 *
 * Aufgerufen aus der sse_listener-Task. NICHT blockieren!
 * Schwere Arbeit in eine separate Queue stellen.
 */
typedef void (*sse_event_cb_t)(const char *event_name, const char *data);

/**
 * Startet den SSE-Listener fuer einen Pfad auf dem unifix-Server.
 *
 * Spawned eine FreeRTOS-Task die sich verbindet, Events parst
 * und reconnected bei Connection-Loss.
 *
 * Kann mehrfach aufgerufen werden - jeder Call startet eine neue Task.
 * Aktuelle Implementation NICHT thread-safe fuer Stop/Restart.
 *
 * @param path        URL-Pfad relativ zum Server, z.B. "/esp/events"
 * @param callback    Funktion die bei jedem Event aufgerufen wird
 *
 * @return ESP_OK              Task erfolgreich gespawned
 * @return ESP_ERR_INVALID_ARG path oder callback NULL
 * @return ESP_ERR_NO_MEM      Task-Creation failed
 */
esp_err_t sse_client_start(const char *path, sse_event_cb_t callback);

#ifdef __cplusplus
}
#endif
