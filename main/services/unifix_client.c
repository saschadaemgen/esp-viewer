/*
 * unifix_client.c - HTTP-Client Implementation
 */

#include "unifix_client.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"

#include "cJSON.h"

#include "device_token.h"
#include "unifix_config.h"   /* fuer unifix_config_mode_to_str */

static const char *TAG = "UNIFIX";

#define UNIFIX_SERVER_HOST   "192.168.1.42"
#define UNIFIX_SERVER_PORT   9080
#define UNIFIX_BASE_URL      "http://192.168.1.42:9080"

#define HTTP_TIMEOUT_MS      5000
#define RESPONSE_BUF_SIZE    1024

/*
 * State:
 *   s_token         Geladener Bearer-Token aus NVS
 *   s_token_loaded  Flag ob Token erfolgreich geladen wurde
 *
 * Token bleibt im RAM solange das Geraet laeuft. Bei Restart neu
 * laden. Wir loggen ihn NIE.
 */
static char s_token[DEVICE_TOKEN_MAX_LEN] = {0};
static bool s_token_loaded = false;

/*
 * Buffer fuer Response-Body, statisch um Heap-Fragmentierung
 * zu vermeiden bei haeufigen Heartbeats.
 */
static char s_response_buf[RESPONSE_BUF_SIZE];
static size_t s_response_len = 0;

esp_err_t unifix_client_init(void)
{
    memset(s_token, 0, sizeof(s_token));
    s_token_loaded = false;

    esp_err_t err = device_token_get(s_token, sizeof(s_token));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load token: %s", esp_err_to_name(err));
        return err;
    }

    s_token_loaded = true;
    ESP_LOGI(TAG, "Client initialized (token length %d)", (int)strlen(s_token));
    return ESP_OK;
}

/*
 * HTTP-Event-Handler fuer esp_http_client.
 * Sammelt Response-Body in s_response_buf.
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGW(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "Connected to server");
        s_response_len = 0;
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "Header: %s: %s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            size_t space = RESPONSE_BUF_SIZE - s_response_len - 1;
            size_t copy = (evt->data_len < space) ? evt->data_len : space;
            if (copy > 0) {
                memcpy(s_response_buf + s_response_len, evt->data, copy);
                s_response_len += copy;
                s_response_buf[s_response_len] = 0;
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH (response_len=%d)", (int)s_response_len);
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "Disconnected");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/*
 * Hilfsfunktion: baut den Authorization-Header-Value.
 * Caller muss buf bereitstellen, mindestens 8 + token_len + 1 Bytes.
 */
static void build_auth_header(char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "Bearer %s", s_token);
}

esp_err_t unifix_client_heartbeat(void)
{
    if (!s_token_loaded) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s/esp/heartbeat", UNIFIX_BASE_URL);

    char auth_header[DEVICE_TOKEN_MAX_LEN + 16];
    build_auth_header(auth_header, sizeof(auth_header));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");

    /* Auth-Header sofort aus Stack ueberschreiben (Defense in Depth) */
    memset(auth_header, 0, sizeof(auth_header));

    s_response_len = 0;
    s_response_buf[0] = 0;

    esp_err_t err = esp_http_client_perform(client);

    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);

    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Heartbeat HTTP failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Heartbeat status=%d, content_length=%d, body='%.*s'",
             status, content_length,
             (int)s_response_len, s_response_buf);

    if (status == 401) {
        ESP_LOGE(TAG, "Heartbeat: Unauthorized (token rejected)");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Heartbeat: bad status %d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t unifix_client_reject(const char *event_id)
{
    if (!s_token_loaded) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!event_id || event_id[0] == 0) {
        ESP_LOGE(TAG, "reject: empty event_id");
        return ESP_ERR_INVALID_ARG;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s/esp/reject", UNIFIX_BASE_URL);

    char body[256];
    int body_len = snprintf(body, sizeof(body),
                            "{\"event_id\":\"%s\"}", event_id);
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        ESP_LOGE(TAG, "reject: body buffer too small");
        return ESP_ERR_INVALID_ARG;
    }

    char auth_header[DEVICE_TOKEN_MAX_LEN + 16];
    build_auth_header(auth_header, sizeof(auth_header));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_post_field(client, body, body_len);

    /* Auth-Header sofort aus Stack ueberschreiben (Defense in Depth) */
    memset(auth_header, 0, sizeof(auth_header));

    s_response_len = 0;
    s_response_buf[0] = 0;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reject HTTP failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reject status=%d, body='%.*s'",
             status, (int)s_response_len, s_response_buf);

    if (status == 401) {
        ESP_LOGE(TAG, "Reject: Unauthorized (token rejected)");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Reject: bad status %d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t unifix_client_unlock(const char *event_id)
{
    if (!s_token_loaded) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!event_id || event_id[0] == 0) {
        ESP_LOGE(TAG, "unlock: empty event_id");
        return ESP_ERR_INVALID_ARG;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s/esp/unlock", UNIFIX_BASE_URL);

    char body[256];
    int body_len = snprintf(body, sizeof(body),
                            "{\"event_id\":\"%s\"}", event_id);
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        ESP_LOGE(TAG, "unlock: body buffer too small");
        return ESP_ERR_INVALID_ARG;
    }

    char auth_header[DEVICE_TOKEN_MAX_LEN + 16];
    build_auth_header(auth_header, sizeof(auth_header));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_post_field(client, body, body_len);

    /* Auth-Header sofort aus Stack ueberschreiben (Defense in Depth) */
    memset(auth_header, 0, sizeof(auth_header));

    s_response_len = 0;
    s_response_buf[0] = 0;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unlock HTTP failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Unlock status=%d, body='%.*s'",
             status, (int)s_response_len, s_response_buf);

    if (status == 401) {
        ESP_LOGE(TAG, "Unlock: Unauthorized (token rejected)");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Unlock: bad status %d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}


/* ============================================================
 * POST /esp/settings
 * ============================================================ */

esp_err_t unifix_client_post_settings(idle_view_mode_t idle_view_mode,
                                       int auto_screensaver_seconds,
                                       int screen_off_after_sec,
                                       int brightness_idle,
                                       const char *language,
                                       const char *clock_layout)
{
    if (!s_token_loaded) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!language) {
        ESP_LOGE(TAG, "post_settings: language is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (!clock_layout) {
        ESP_LOGE(TAG, "post_settings: clock_layout is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Clamp brightness defensively (server validates too but no need
     * to send obviously-bad values). */
    if (brightness_idle < 0) brightness_idle = 0;
    if (brightness_idle > 100) brightness_idle = 100;

    char url[128];
    snprintf(url, sizeof(url), "%s/esp/settings", UNIFIX_BASE_URL);

    char body[384];
    int body_len = snprintf(body, sizeof(body),
        "{"
        "\"idle_view_mode\":\"%s\","
        "\"auto_screensaver_seconds\":%d,"
        "\"screen_off_after_sec\":%d,"
        "\"brightness_idle\":%d,"
        "\"language\":\"%s\","
        "\"clock_layout\":\"%s\""
        "}",
        unifix_config_mode_to_str(idle_view_mode),
        auto_screensaver_seconds,
        screen_off_after_sec,
        brightness_idle,
        language,
        clock_layout);

    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        ESP_LOGE(TAG, "post_settings: body buffer too small");
        return ESP_ERR_INVALID_ARG;
    }

    char auth_header[DEVICE_TOKEN_MAX_LEN + 16];
    build_auth_header(auth_header, sizeof(auth_header));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        memset(auth_header, 0, sizeof(auth_header));
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_post_field(client, body, body_len);

    memset(auth_header, 0, sizeof(auth_header));

    s_response_len = 0;
    s_response_buf[0] = 0;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "post_settings HTTP failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "post_settings status=%d, body='%.*s'",
             status, (int)s_response_len, s_response_buf);

    if (status == 401) {
        ESP_LOGE(TAG, "post_settings: Unauthorized");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status == 400) {
        ESP_LOGE(TAG, "post_settings: server rejected values (400)");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "post_settings: bad status %d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}


/* ============================================================
 * GET /esp/weather
 * ============================================================ */

esp_err_t unifix_client_get_weather(unifix_weather_t *out)
{
    if (!s_token_loaded) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Pre-fill defaults so partial parses still yield a usable struct */
    memset(out, 0, sizeof(*out));

    char url[128];
    snprintf(url, sizeof(url), "%s/esp/weather", UNIFIX_BASE_URL);

    char auth_header[DEVICE_TOKEN_MAX_LEN + 16];
    build_auth_header(auth_header, sizeof(auth_header));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        memset(auth_header, 0, sizeof(auth_header));
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");

    memset(auth_header, 0, sizeof(auth_header));

    s_response_len = 0;
    s_response_buf[0] = 0;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_weather HTTP failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (status == 401) {
        ESP_LOGE(TAG, "get_weather: Unauthorized");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "get_weather: bad status %d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* TEMP DEBUG S03-10: rohen Response loggen zur Diagnose.
     * Server liefert condition_text + icon_code leer - wir muessen
     * sehen ob das schon im Wire-Format leer ist oder ob unser
     * Parser die Felder verfehlt. Entfernen sobald geklaert. */
    ESP_LOGI(TAG, "get_weather RAW response: %s", s_response_buf);

    /* Parse JSON response */
    cJSON *root = cJSON_Parse(s_response_buf);
    if (!root) {
        ESP_LOGE(TAG, "get_weather: JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *temp = cJSON_GetObjectItem(root, "temp_c");
    if (cJSON_IsNumber(temp)) {
        out->temp_c = temp->valueint;
    }

    cJSON *cond = cJSON_GetObjectItem(root, "condition_text");
    if (cJSON_IsString(cond) && cond->valuestring) {
        snprintf(out->condition_text, sizeof(out->condition_text),
                 "%s", cond->valuestring);
    }

    cJSON *icon = cJSON_GetObjectItem(root, "icon_code");
    if (cJSON_IsString(icon) && icon->valuestring) {
        snprintf(out->icon_code, sizeof(out->icon_code),
                 "%s", icon->valuestring);
    }

    cJSON *upd = cJSON_GetObjectItem(root, "updated_at");
    if (cJSON_IsNumber(upd)) {
        out->updated_at = (int64_t)upd->valuedouble;
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Weather: %d°C %s (%s) @ %lld",
             out->temp_c, out->condition_text, out->icon_code,
             (long long)out->updated_at);

    return ESP_OK;
}


/* ============================================================
 * GET /esp/unread-count
 * ============================================================ */

esp_err_t unifix_client_get_unread_count(int *out_count)
{
    if (!s_token_loaded) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!out_count) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0;

    char url[128];
    snprintf(url, sizeof(url), "%s/esp/unread-count", UNIFIX_BASE_URL);

    char auth_header[DEVICE_TOKEN_MAX_LEN + 16];
    build_auth_header(auth_header, sizeof(auth_header));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        memset(auth_header, 0, sizeof(auth_header));
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");

    memset(auth_header, 0, sizeof(auth_header));

    s_response_len = 0;
    s_response_buf[0] = 0;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_unread_count HTTP failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (status == 401) {
        ESP_LOGE(TAG, "get_unread_count: Unauthorized");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "get_unread_count: bad status %d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *root = cJSON_Parse(s_response_buf);
    if (!root) {
        ESP_LOGE(TAG, "get_unread_count: JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *count = cJSON_GetObjectItem(root, "count");
    if (cJSON_IsNumber(count)) {
        *out_count = count->valueint;
    } else {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "get_unread_count: missing 'count' field");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Unread count: %d", *out_count);
    return ESP_OK;
}
