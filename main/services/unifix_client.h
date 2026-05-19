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
#include <stddef.h>
#include "esp_err.h"
#include "unifix_config.h"   /* fuer idle_view_mode_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Field-size limits fuer Output-Buffers ---------------- */

#define UNIFIX_WEATHER_CONDITION_MAX  64
#define UNIFIX_WEATHER_ICON_CODE_MAX  32

/* ---------------- Wetter-Snapshot ---------------- */

typedef struct {
    int     temp_c;
    char    condition_text[UNIFIX_WEATHER_CONDITION_MAX];
    char    icon_code[UNIFIX_WEATHER_ICON_CODE_MAX];
    int64_t updated_at;   /* Unix-Sekunden vom Server */
} unifix_weather_t;

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

/**
 * POST /esp/unlock
 *
 * Loest die Tueroeffnung aus. Der Server bestimmt die zu oeffnende Tuer
 * selbststaendig via paired_intercom_mac (Auto-Door-Resolution seit
 * Master-S13-07).
 *
 * Body: {"event_id":"<cancel_token>"}
 *
 * @param event_id  Der cancel_token aus dem SSE doorbell.ring Event
 *
 * @return ESP_OK                   Server hat unlock akzeptiert (HTTP 2xx)
 * @return ESP_ERR_INVALID_STATE    Client nicht initialisiert
 * @return ESP_ERR_INVALID_ARG      event_id NULL, leer oder zu lang
 * @return ESP_ERR_INVALID_RESPONSE Server hat mit Fehler-Status geantwortet
 * @return ESP_FAIL                 Netzwerk-Fehler
 */
esp_err_t unifix_client_unlock(const char *event_id);

/**
 * POST /esp/settings
 *
 * Persistiert die Idle-Mode- und Hardware-Settings am Server. Server
 * validiert die Werte gegen feste Allow-Listen (siehe Master-Chat-Spec
 * S14-XX). Bei Erfolg triggert der Server automatisch ein 
 * config.changed-SSE-Event, das ALLE Geraete des selben Mieters 
 * erhalten (inklusive uns selbst - eigenes Echo).
 *
 * Body (Master-Chat-Spec + S03-09):
 *   {
 *     "idle_view_mode":           "screensaver" | "livestream" | "screen_off",
 *     "auto_screensaver_seconds":  0|30|60|300|600,
 *     "screen_off_after_sec":     0|30|60|300|600|1800,
 *     "brightness_idle":           0..100,
 *     "language":                 "de"|"en",
 *     "clock_layout":             "vertical"|"horizontal"
 *   }
 *
 * Wir senden immer alle Felder mit (kein Partial-Update auf
 * Client-Seite). Server akzeptiert das.
 *
 * @return ESP_OK                   Server hat persistiert (HTTP 200)
 * @return ESP_ERR_INVALID_STATE    Client nicht initialisiert
 * @return ESP_ERR_INVALID_ARG      language oder clock_layout NULL
 * @return ESP_ERR_INVALID_RESPONSE HTTP 4xx/5xx (z.B. 400 bei Wert
 *                                  ausserhalb Allow-List, 401 bei
 *                                  abgelehntem Token)
 * @return ESP_FAIL                 Netzwerk-Fehler
 */
esp_err_t unifix_client_post_settings(idle_view_mode_t idle_view_mode,
                                       int auto_screensaver_seconds,
                                       int screen_off_after_sec,
                                       int brightness_idle,
                                       const char *language,
                                       const char *clock_layout);

/**
 * GET /esp/weather
 *
 * Holt die aktuellen Wetter-Daten fuer den Standort des Viewers.
 * Server-Cache 15min; Polling alle 15min vom ESP reicht.
 * Zusaetzlich bei jedem config.changed-Event neu fetchen, weil sich
 * der Standort geaendert haben koennte.
 *
 * Response (Master-Chat-Spec):
 *   {
 *     "temp_c":         12,
 *     "condition_text": "Bewölkt",
 *     "icon_code":      "cloud",
 *     "updated_at":     1779030000
 *   }
 *
 * @param out  Caller-allocated Struct, wird bei ESP_OK gefuellt.
 *
 * @return ESP_OK                   Daten geholt und in out kopiert
 * @return ESP_ERR_INVALID_STATE    Client nicht initialisiert
 * @return ESP_ERR_INVALID_ARG      out NULL
 * @return ESP_ERR_INVALID_RESPONSE HTTP-Fehler oder JSON-Parse-Fehler
 * @return ESP_FAIL                 Netzwerk-Fehler
 */
esp_err_t unifix_client_get_weather(unifix_weather_t *out);

/**
 * GET /esp/unread-count
 *
 * Holt die Anzahl unbeantworteter Klingel-Ereignisse fuer diesen 
 * Viewer. Beim Boot einmal fetchen, danach via SSE unread_count-Event
 * aktuell halten.
 *
 * Response: {"count": <int>}
 *
 * @param out_count  Caller-allocated, wird bei ESP_OK gesetzt.
 *
 * @return ESP_OK                   Count geholt
 * @return ESP_ERR_INVALID_STATE    Client nicht initialisiert
 * @return ESP_ERR_INVALID_ARG      out_count NULL
 * @return ESP_ERR_INVALID_RESPONSE HTTP- oder Parse-Fehler
 * @return ESP_FAIL                 Netzwerk-Fehler
 */
esp_err_t unifix_client_get_unread_count(int *out_count);

#ifdef __cplusplus
}
#endif
