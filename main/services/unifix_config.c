/*
 * unifix_config.c - /esp/config Konsumption Implementation
 */

#include "unifix_config.h"
#include "config_cache.h"
#include "device_token.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "cJSON.h"

static const char *TAG = "CONFIG";

#define UNIFIX_BASE_URL              "http://192.168.1.42:9080"
#define UNIFIX_CONFIG_PATH           "/esp/config"
#define HTTP_TIMEOUT_MS              5000
#define UNIFIX_CONFIG_MAX_LISTENERS  4

/* ---------------- Module state ---------------- */

static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;
static unifix_config_t s_state;

static unifix_config_listener_t s_listeners[UNIFIX_CONFIG_MAX_LISTENERS] = {0};
static int s_listener_count = 0;

/* HTTP response buffer for /esp/config */
static char s_resp_buf[CONFIG_CACHE_MAX_LEN];
static size_t s_resp_len = 0;

/* ---------------- Mode-Helper ---------------- */

const char *unifix_config_mode_to_str(idle_view_mode_t mode)
{
    switch (mode) {
    case IDLE_MODE_LIVESTREAM:  return "livestream";
    case IDLE_MODE_SCREEN_OFF:  return "screen_off";
    case IDLE_MODE_SCREENSAVER:
    default:
        return "screensaver";
    }
}

idle_view_mode_t unifix_config_mode_from_str(const char *s)
{
    if (!s) return IDLE_MODE_SCREENSAVER;
    if (strcmp(s, "livestream") == 0) return IDLE_MODE_LIVESTREAM;
    if (strcmp(s, "screen_off") == 0) return IDLE_MODE_SCREEN_OFF;
    return IDLE_MODE_SCREENSAVER;
}

/* ---------------- Clock-Layout-Helper (S03-09) ---------------- */

const char *unifix_config_clock_layout_to_str(clock_layout_t v)
{
    switch (v) {
    case CLOCK_LAYOUT_HORIZONTAL:  return "horizontal";
    case CLOCK_LAYOUT_VERTICAL:
    default:
        return "vertical";
    }
}

clock_layout_t unifix_config_clock_layout_from_str(const char *s)
{
    if (s && strcmp(s, "horizontal") == 0) return CLOCK_LAYOUT_HORIZONTAL;
    return CLOCK_LAYOUT_VERTICAL;
}

/* ---------------- Defaults helper ---------------- */

static void apply_defaults(unifix_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->mieter_name,   sizeof(cfg->mieter_name),
             "%s", UNIFIX_CONFIG_DEFAULT_MIETER_NAME);
    snprintf(cfg->location_name, sizeof(cfg->location_name),
             "%s", UNIFIX_CONFIG_DEFAULT_LOCATION_NAME);
    snprintf(cfg->language,      sizeof(cfg->language),
             "%s", UNIFIX_CONFIG_DEFAULT_LANGUAGE);
    cfg->brightness_idle           = UNIFIX_CONFIG_DEFAULT_BRIGHTNESS_IDLE;
    cfg->auto_screensaver_seconds  = UNIFIX_CONFIG_DEFAULT_AUTO_SCREENSAVER_SEC;
    cfg->screen_off_after_sec      = UNIFIX_CONFIG_DEFAULT_SCREEN_OFF_SEC;
    cfg->idle_view_mode            = UNIFIX_CONFIG_DEFAULT_IDLE_VIEW_MODE;
    cfg->clock_layout              = UNIFIX_CONFIG_DEFAULT_CLOCK_LAYOUT;
}

/* ---------------- Lifecycle ---------------- */

esp_err_t unifix_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "init: failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    apply_defaults(&s_state);
    memset(s_listeners, 0, sizeof(s_listeners));
    s_listener_count = 0;

    s_initialized = true;
    ESP_LOGI(TAG, "Module initialized with defaults "
             "(mieter='%s', location='%s', lang='%s', brightness=%d, "
             "idle_mode=%s, auto_screensaver=%ds, screen_off=%ds)",
             s_state.mieter_name, s_state.location_name, s_state.language,
             s_state.brightness_idle,
             unifix_config_mode_to_str(s_state.idle_view_mode),
             s_state.auto_screensaver_seconds,
             s_state.screen_off_after_sec);
    return ESP_OK;
}

/* ---------------- JSON parsing helper ---------------- */

/*
 * Parse a JSON string into a unifix_config_t.
 * Missing or wrong-typed fields fall back to the CURRENT value in *out
 * (caller pre-fills with defaults or current state).
 *
 * Schema (Master-Chat S14-XX + S03-09):
 * {
 *   "viewer": { "name": "...", "mac": "...", ... },
 *   "ui": {
 *     "language": "de",
 *     "idle_view_mode": "screensaver"|"livestream"|"screen_off",
 *     "auto_screensaver_seconds": 60,
 *     "screensaver_after_sec": 60,         // legacy alias, same value
 *     "screen_off_after_sec": 300,
 *     "brightness_idle": 70,
 *     "clock_layout": "vertical"|"horizontal"     // S03-09 default vertical
 *   },
 *   "doors": [ ... ],
 *   "weather": { ... }    // ignored here, separate /esp/weather endpoint
 * }
 *
 * Backwards-compat: an older response with top-level "mieter_name" /
 * "location_name" is still honored if present (for stale NVS caches
 * from pre-S14 firmware).
 */
static esp_err_t parse_json_into(const char *json_str, unifix_config_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    /* Mieter-Name: prefer viewer.name, fallback to legacy mieter_name */
    cJSON *viewer = cJSON_GetObjectItem(root, "viewer");
    if (cJSON_IsObject(viewer)) {
        cJSON *vname = cJSON_GetObjectItem(viewer, "name");
        if (cJSON_IsString(vname) && vname->valuestring) {
            snprintf(out->mieter_name, sizeof(out->mieter_name),
                     "%s", vname->valuestring);
        }
    } else {
        cJSON *legacy_mn = cJSON_GetObjectItem(root, "mieter_name");
        if (cJSON_IsString(legacy_mn) && legacy_mn->valuestring) {
            snprintf(out->mieter_name, sizeof(out->mieter_name),
                     "%s", legacy_mn->valuestring);
        }
    }

    /* Location-Name: heute hartcoded "Hauseingang" serverseitig.
     * Wenn doors[] mit name kommt, koennten wir das spaeter daraus
     * ziehen. Heute: respect top-level location_name fallback nur
     * fuer alte Caches. */
    cJSON *ln = cJSON_GetObjectItem(root, "location_name");
    if (cJSON_IsString(ln) && ln->valuestring && ln->valuestring[0] != 0) {
        snprintf(out->location_name, sizeof(out->location_name),
                 "%s", ln->valuestring);
    }
    /* Master-Chat: doors[0].name als fallback fuer location_name */
    cJSON *doors = cJSON_GetObjectItem(root, "doors");
    if (cJSON_IsArray(doors) && cJSON_GetArraySize(doors) > 0) {
        cJSON *door0 = cJSON_GetArrayItem(doors, 0);
        cJSON *dname = cJSON_GetObjectItem(door0, "name");
        if (cJSON_IsString(dname) && dname->valuestring && dname->valuestring[0] != 0) {
            snprintf(out->location_name, sizeof(out->location_name),
                     "%s", dname->valuestring);
        }
    }

    cJSON *ui = cJSON_GetObjectItem(root, "ui");
    if (cJSON_IsObject(ui)) {
        cJSON *lang = cJSON_GetObjectItem(ui, "language");
        if (cJSON_IsString(lang) && lang->valuestring && lang->valuestring[0] != 0) {
            snprintf(out->language, sizeof(out->language), "%s", lang->valuestring);
        }

        cJSON *bi = cJSON_GetObjectItem(ui, "brightness_idle");
        if (cJSON_IsNumber(bi)) {
            int v = bi->valueint;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            out->brightness_idle = v;
        }

        /* idle_view_mode */
        cJSON *ivm = cJSON_GetObjectItem(ui, "idle_view_mode");
        if (cJSON_IsString(ivm) && ivm->valuestring) {
            out->idle_view_mode = unifix_config_mode_from_str(ivm->valuestring);
        }

        /* auto_screensaver_seconds (canonical), legacy screensaver_after_sec
         * as fallback. Server liefert beide mit identischem Wert. */
        cJSON *ass = cJSON_GetObjectItem(ui, "auto_screensaver_seconds");
        if (cJSON_IsNumber(ass)) {
            int v = ass->valueint;
            if (v < 0) v = 0;
            out->auto_screensaver_seconds = v;
        } else {
            cJSON *legacy = cJSON_GetObjectItem(ui, "screensaver_after_sec");
            if (cJSON_IsNumber(legacy)) {
                int v = legacy->valueint;
                if (v < 0) v = 0;
                out->auto_screensaver_seconds = v;
            }
        }

        /* screen_off_after_sec - ESP-only field */
        cJSON *soa = cJSON_GetObjectItem(ui, "screen_off_after_sec");
        if (cJSON_IsNumber(soa)) {
            int v = soa->valueint;
            if (v < 0) v = 0;
            out->screen_off_after_sec = v;
        }

        /* clock_layout (S03-09) - fehlend bedeutet Server hat das Feld
         * noch nicht. Default-Fallback bleibt der aktuelle out->clock_layout
         * (vom Caller mit Defaults vorbefuellt). */
        cJSON *cl = cJSON_GetObjectItem(ui, "clock_layout");
        if (cJSON_IsString(cl) && cl->valuestring) {
            out->clock_layout = unifix_config_clock_layout_from_str(cl->valuestring);
        }
    }

    /*
     * doors[], weather, viewer.{mac,type,paired_intercom_mac} - parsed
     * structurally for validation but not consumed in this struct.
     * Weather is fetched via the dedicated /esp/weather endpoint.
     */

    cJSON_Delete(root);
    return ESP_OK;
}

/* ---------------- Cache load ---------------- */

esp_err_t unifix_config_load_from_cache(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* CONFIG_CACHE_MAX_LEN is 4096 bytes; allocating that on the caller's
     * stack overflows small tasks like sys_evt (where on_got_ip lives).
     * Use the heap instead. */
    char *buf = malloc(CONFIG_CACHE_MAX_LEN);
    if (!buf) {
        ESP_LOGE(TAG, "load_from_cache: malloc failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = config_cache_load(buf, CONFIG_CACHE_MAX_LEN);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No cached config, sticking with defaults");
        free(buf);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cache load failed: %s, keeping defaults", esp_err_to_name(err));
        free(buf);
        return err;
    }

    unifix_config_t parsed;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        free(buf);
        return ESP_FAIL;
    }
    parsed = s_state; /* start from current state (defaults if init only) */
    xSemaphoreGive(s_mutex);

    err = parse_json_into(buf, &parsed);
    free(buf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cached JSON unparseable, keeping current state");
        return err;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    s_state = parsed;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Config loaded from cache "
             "(mieter='%s', location='%s', brightness=%d, "
             "idle_mode=%s, auto_screensaver=%ds, screen_off=%ds)",
             s_state.mieter_name, s_state.location_name,
             s_state.brightness_idle,
             unifix_config_mode_to_str(s_state.idle_view_mode),
             s_state.auto_screensaver_seconds,
             s_state.screen_off_after_sec);
    return ESP_OK;
}

/* ---------------- HTTP fetch ---------------- */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        s_resp_len = 0;
        s_resp_buf[0] = 0;
        break;
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            size_t space = sizeof(s_resp_buf) - s_resp_len - 1;
            size_t copy = (evt->data_len < space) ? evt->data_len : space;
            if (copy > 0) {
                memcpy(s_resp_buf + s_resp_len, evt->data, copy);
                s_resp_len += copy;
                s_resp_buf[s_resp_len] = 0;
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void notify_listeners(const unifix_config_t *cfg)
{
    /* Snapshot listeners under lock, then call them WITHOUT the lock
     * to avoid deadlock if a listener calls unifix_config_get(). */
    unifix_config_listener_t local[UNIFIX_CONFIG_MAX_LISTENERS] = {0};
    int n = 0;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < s_listener_count; i++) {
            local[n++] = s_listeners[i];
        }
        xSemaphoreGive(s_mutex);
    }

    for (int i = 0; i < n; i++) {
        if (local[i]) local[i](cfg);
    }
}

esp_err_t unifix_config_fetch_and_apply(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char token[DEVICE_TOKEN_MAX_LEN] = {0};
    esp_err_t err = device_token_get(token, sizeof(token));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fetch: device_token_get failed");
        return err;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s%s", UNIFIX_BASE_URL, UNIFIX_CONFIG_PATH);

    char auth_header[DEVICE_TOKEN_MAX_LEN + 16];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    memset(token, 0, sizeof(token));

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

    s_resp_len = 0;
    s_resp_buf[0] = 0;

    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fetch HTTP failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (status == 401) {
        ESP_LOGE(TAG, "fetch: 401 Unauthorized");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "fetch: bad status %d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "fetched %d bytes from /esp/config", (int)s_resp_len);

    /* Save raw JSON to NVS cache BEFORE parsing - if parse fails,
     * we still have the cached payload for forensics. */
    esp_err_t cache_err = config_cache_save(s_resp_buf);
    if (cache_err != ESP_OK) {
        ESP_LOGW(TAG, "cache save failed: %s (continuing)", esp_err_to_name(cache_err));
    }

    /* Parse into a local copy of current state (so unset fields keep
     * their current value, not defaults). */
    unifix_config_t parsed;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    parsed = s_state;
    xSemaphoreGive(s_mutex);

    err = parse_json_into(s_resp_buf, &parsed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "parse failed");
        return err;
    }

    /* Commit to module state */
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    s_state = parsed;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Config applied "
             "(mieter='%s', location='%s', lang='%s', brightness=%d, "
             "idle_mode=%s, auto_screensaver=%ds, screen_off=%ds, "
             "clock_layout=%s)",
             parsed.mieter_name, parsed.location_name, parsed.language,
             parsed.brightness_idle,
             unifix_config_mode_to_str(parsed.idle_view_mode),
             parsed.auto_screensaver_seconds,
             parsed.screen_off_after_sec,
             unifix_config_clock_layout_to_str(parsed.clock_layout));

    notify_listeners(&parsed);
    return ESP_OK;
}

/* ---------------- Getter ---------------- */

esp_err_t unifix_config_get(unifix_config_t *out_cfg)
{
    if (!out_cfg) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    *out_cfg = s_state;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

/* ---------------- Listener registration ---------------- */

esp_err_t unifix_config_register_listener(unifix_config_listener_t cb)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    /* Deduplicate */
    for (int i = 0; i < s_listener_count; i++) {
        if (s_listeners[i] == cb) {
            xSemaphoreGive(s_mutex);
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (s_listener_count >= UNIFIX_CONFIG_MAX_LISTENERS) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "register_listener: table full");
        return ESP_ERR_NO_MEM;
    }

    s_listeners[s_listener_count++] = cb;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Listener registered (total %d)", s_listener_count);
    return ESP_OK;
}
