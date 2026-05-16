/*
 * main.c - KOENIGSLIGA UniFi-Display
 *
 * Architecture:
 *   - Loading screen is an OWN lv_obj_t (created with lv_obj_create(NULL))
 *   - Idle screen is ANOTHER own lv_obj_t (created the same way)
 *   - LVGL switches between them with lv_screen_load_anim - no shared
 *     parent, no lv_obj_clean side effects, no dangling pointers.
 *
 * Boot flow:
 *   1. NVS + display init
 *   2. scr_loading_create -> activates loading screen
 *   3. status = "Starte"
 *   4. Setup-mode check (if no token in NVS, abort)
 *   5. status = "Verbinde WLAN", wifi_init()
 *   6. On reconnects: status = "Reconnect N"
 *   7. On IP_GOT:
 *      - status = "Verbunden"
 *      - Build idle screen on its own handle (NOT active yet)
 *      - Build ringing overlay on the idle screen (hidden)
 *      - Wire reject button click handler
 *      - status = "Lade Stream"
 *      - unifix_client_init + heartbeat
 *      - Create unifix action queue + worker task
 *      - SSE listener
 *      - stream_pipeline_start(stream_slot)
 *      - 500ms later: lv_screen_load_anim(idle_screen, FADE, 400ms)
 *
 * Unifix-Action-Dispatch:
 *   - User-clicks on ringing overlay get an "optimistic UI" treatment:
 *     overlay hides immediately, request is enqueued for the worker.
 *   - unifix_action_worker is a permanent FreeRTOS task that drains the
 *     queue and runs the corresponding HTTP call (reject, answer, unlock).
 *   - This keeps HTTP latency off the LVGL thread.
 */

#include <string.h>
#include <stdio.h>

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "cJSON.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#include "stream_pipeline.h"
#include "services/device_token.h"
#include "services/setup_mode.h"
#include "services/unifix_client.h"
#include "services/sse_client.h"

#include "ui/scr_loading.h"
#include "ui/scr_idle.h"
#include "ui/scr_ringing.h"

static const char *TAG = "KOENIG";

#define WIFI_SSID       "SONCLOUD"
#define WIFI_PASSWORD   "MKwlan1d250e-mured5"

#define PLACEHOLDER_UNIT_NAME   "Daemgen"
#define PLACEHOLDER_DOOR_NAME   "Hauseingang"
#define PLACEHOLDER_NOW         "14:23:01"
#define PLACEHOLDER_NOW_DATE    "Fr, 15. Mai"

#define LOADING_TO_IDLE_DELAY_MS  500
#define SCREEN_FADE_DURATION_MS   400

#define CANCEL_TOKEN_BUF_SIZE     64
#define UNIFIX_QUEUE_DEPTH        4
#define UNIFIX_WORKER_STACK       4096
#define UNIFIX_WORKER_PRIO        5
#define UNIFIX_WORKER_CORE        1

static int s_retry = 0;
static lv_obj_t *s_ringing_overlay = NULL;
static lv_obj_t *s_idle_screen     = NULL;

/*
 * Active cancel_token from the most recent SSE doorbell.ring event.
 * Cleared on doorbell.cancel. Read by the reject click handler.
 *
 * Race-Condition note: SSE task writes, LVGL task reads. In practice
 * there is no overlap: SSE writes once at ring time, then sits idle
 * until the user reacts. LVGL reads only on click, after the user
 * has seen the overlay. We accept the theoretical race without a mutex.
 */
static char s_active_cancel_token[CANCEL_TOKEN_BUF_SIZE] = {0};


/* ============================================================
 * Unifix-Action-Worker
 *
 * Single permanent FreeRTOS task that drains a queue of pending
 * unifix-API actions and executes the corresponding HTTP call.
 * Keeps HTTP latency off the LVGL thread.
 * ============================================================ */
typedef enum {
    UNIFIX_ACTION_REJECT,
    UNIFIX_ACTION_UNLOCK,
    /* UNIFIX_ACTION_ANSWER -> spaeter mit Audio */
} unifix_action_t;

typedef struct {
    unifix_action_t action;
    char event_id[CANCEL_TOKEN_BUF_SIZE];
} unifix_request_t;

static QueueHandle_t s_unifix_action_queue = NULL;

static void unifix_action_worker(void *arg)
{
    (void)arg;
    unifix_request_t req;

    ESP_LOGI(TAG, "Unifix action worker running");

    while (1) {
        if (xQueueReceive(s_unifix_action_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (req.action) {
        case UNIFIX_ACTION_REJECT:
            ESP_LOGI(TAG, "Worker: dispatching REJECT");
            unifix_client_reject(req.event_id);
            break;
        case UNIFIX_ACTION_UNLOCK:
            ESP_LOGI(TAG, "Worker: dispatching UNLOCK");
            unifix_client_unlock(req.event_id);
            break;
        default:
            ESP_LOGW(TAG, "Worker: unknown action %d", req.action);
            break;
        }
        /* Wipe the buffer so the token does not linger in RAM. */
        memset(&req, 0, sizeof(req));
    }
}


/* ============================================================
 * Reject button click handler
 *
 * Runs on the LVGL task. Hides the overlay immediately for
 * snappy UX, then enqueues the REJECT request for the worker.
 * ============================================================ */
static void on_reject_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Reject button pressed");

    /* Optimistic UI: hide the overlay immediately */
    if (s_ringing_overlay) {
        lv_obj_add_flag(s_ringing_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_active_cancel_token[0] == 0) {
        ESP_LOGW(TAG, "Reject: no active cancel_token, skipping POST");
        return;
    }
    if (!s_unifix_action_queue) {
        ESP_LOGE(TAG, "Reject: action queue not ready");
        return;
    }

    unifix_request_t req = { .action = UNIFIX_ACTION_REJECT };
    snprintf(req.event_id, sizeof(req.event_id), "%s", s_active_cancel_token);

    if (xQueueSend(s_unifix_action_queue, &req, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Reject: action queue full");
    }
}


/* ============================================================
 * Unlock button click handler
 *
 * Runs on the LVGL task. Hides the overlay immediately (optimistic
 * UI: door is being opened, no need to keep showing the ring), then
 * enqueues the UNLOCK request for the worker. The server resolves
 * the door via paired_intercom_mac, no event_id needed.
 * ============================================================ */
static void on_unlock_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Unlock button pressed");

    /* Optimistic UI: hide the overlay immediately */
    if (s_ringing_overlay) {
        lv_obj_add_flag(s_ringing_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_active_cancel_token[0] == 0) {
        ESP_LOGW(TAG, "Unlock: no active cancel_token, skipping POST");
        return;
    }
    if (!s_unifix_action_queue) {
        ESP_LOGE(TAG, "Unlock: action queue not ready");
        return;
    }

    unifix_request_t req = { .action = UNIFIX_ACTION_UNLOCK };
    snprintf(req.event_id, sizeof(req.event_id), "%s", s_active_cancel_token);

    if (xQueueSend(s_unifix_action_queue, &req, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Unlock: action queue full");
    }
}


/* ============================================================
 * SSE event callback
 * ============================================================ */
static void on_sse_event(const char *event_name, const char *data)
{
    if (strcmp(event_name, "heartbeat") == 0) {
        ESP_LOGI(TAG, "[SSE] heartbeat: %s", data);
        return;
    }

    if (strcmp(event_name, "doorbell.ring") == 0) {
        ESP_LOGW(TAG, "[SSE] >>> DOORBELL RING <<< %s", data);

        /* Parse cancel_token from event data */
        cJSON *json = cJSON_Parse(data);
        if (json) {
            cJSON *token = cJSON_GetObjectItem(json, "cancel_token");
            if (cJSON_IsString(token) && token->valuestring) {
                strncpy(s_active_cancel_token, token->valuestring,
                        sizeof(s_active_cancel_token) - 1);
                s_active_cancel_token[sizeof(s_active_cancel_token) - 1] = 0;
                ESP_LOGI(TAG, "cancel_token captured (%d chars)",
                         (int)strlen(s_active_cancel_token));
            } else {
                ESP_LOGW(TAG, "doorbell.ring: cancel_token missing or not a string");
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGW(TAG, "doorbell.ring: failed to parse JSON");
        }

        if (s_ringing_overlay) {
            if (bsp_display_lock(200)) {
                lv_obj_clear_flag(s_ringing_overlay, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s_ringing_overlay);
                bsp_display_unlock();
                ESP_LOGI(TAG, "Ringing overlay shown");
            } else {
                ESP_LOGW(TAG, "Ringing show: display_lock timeout");
            }
        } else {
            ESP_LOGE(TAG, "Ringing show: overlay is NULL");
        }
        return;
    }

    if (strcmp(event_name, "doorbell.cancel") == 0) {
        ESP_LOGW(TAG, "[SSE] <<< DOORBELL CANCEL >>> %s", data);
        s_active_cancel_token[0] = 0;
        if (s_ringing_overlay) {
            if (bsp_display_lock(200)) {
                lv_obj_add_flag(s_ringing_overlay, LV_OBJ_FLAG_HIDDEN);
                bsp_display_unlock();
                ESP_LOGI(TAG, "Ringing overlay hidden");
            } else {
                ESP_LOGW(TAG, "Ringing hide: display_lock timeout");
            }
        }
        return;
    }

    ESP_LOGI(TAG, "[SSE] %s: %s", event_name, data);
}


/* ============================================================
 * Switch from loading to idle screen
 * Called from esp_timer service task - takes LVGL lock.
 * ============================================================ */
static void fade_to_idle_cb(void *arg)
{
    (void)arg;
    if (!s_idle_screen) {
        ESP_LOGW(TAG, "fade_to_idle: idle screen not ready");
        return;
    }
    if (!bsp_display_lock(200)) {
        ESP_LOGW(TAG, "fade_to_idle: could not acquire display lock");
        return;
    }
    ESP_LOGI(TAG, "Switching from loading to idle screen (fade %dms)",
             SCREEN_FADE_DURATION_MS);
    lv_screen_load_anim(s_idle_screen,
                        LV_SCR_LOAD_ANIM_FADE_ON,
                        SCREEN_FADE_DURATION_MS,
                        0, false);
    bsp_display_unlock();
}


/* ============================================================
 * On IP got: build idle screen on its own handle (not active yet),
 *            build ringing overlay on top of idle,
 *            wire click handlers,
 *            start unifix worker + SSE + stream pipeline,
 *            then schedule the fade.
 * ============================================================ */
static void on_got_ip(void)
{
    scr_loading_set_status("Verbunden");

    lv_obj_t *stream_slot = NULL;

    if (bsp_display_lock(200)) {
        /* Create idle screen as its own top-level screen (parent=NULL).
         * It is NOT activated yet - lv_screen_load_anim does that later. */
        s_idle_screen = lv_obj_create(NULL);

        scr_idle_data_t idle = {
            .unit_name  = PLACEHOLDER_UNIT_NAME,
            .door_name  = PLACEHOLDER_DOOR_NAME,
            .now        = PLACEHOLDER_NOW,
            .now_date   = PLACEHOLDER_NOW_DATE,
            .dnd        = false,
            .has_unread = false,
        };
        stream_slot = scr_idle_build(s_idle_screen, &idle);

        scr_ringing_data_t ring = {
            .door_name = PLACEHOLDER_DOOR_NAME,
        };
        s_ringing_overlay = scr_ringing_build(s_idle_screen, &ring);
        lv_obj_add_flag(s_ringing_overlay, LV_OBJ_FLAG_HIDDEN);

        /* Wire the ringing overlay button click handlers */
        scr_ringing_set_reject_handler(s_ringing_overlay, on_reject_click, NULL);
        scr_ringing_set_unlock_handler(s_ringing_overlay, on_unlock_click, NULL);

        bsp_display_unlock();
        ESP_LOGI(TAG, "Idle screen + ringing overlay built (overlay=%p)",
                 (void *)s_ringing_overlay);
    }

    scr_loading_set_status("Lade Stream");

    ESP_LOGI(TAG, "Initializing unifix-client ...");
    esp_err_t err = unifix_client_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Probing /esp/heartbeat ...");
        err = unifix_client_heartbeat();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Heartbeat OK - unifix-Server reachable");
        } else {
            ESP_LOGW(TAG, "Heartbeat failed: %s (continuing anyway)",
                     esp_err_to_name(err));
        }

        /* Spawn the unifix action worker BEFORE the SSE listener, so
         * the queue is ready by the time the first doorbell.ring arrives. */
        ESP_LOGI(TAG, "Creating unifix action queue + worker ...");
        s_unifix_action_queue = xQueueCreate(UNIFIX_QUEUE_DEPTH,
                                              sizeof(unifix_request_t));
        if (!s_unifix_action_queue) {
            ESP_LOGE(TAG, "Failed to create unifix action queue");
        } else {
            BaseType_t ok = xTaskCreatePinnedToCore(unifix_action_worker,
                                                     "unifix_worker",
                                                     UNIFIX_WORKER_STACK,
                                                     NULL,
                                                     UNIFIX_WORKER_PRIO,
                                                     NULL,
                                                     UNIFIX_WORKER_CORE);
            if (ok != pdPASS) {
                ESP_LOGE(TAG, "Failed to create unifix worker task");
            }
        }

        ESP_LOGI(TAG, "Starting SSE-Listener for /esp/events ...");
        err = sse_client_start("/esp/events", on_sse_event);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SSE start failed: %s (continuing anyway)",
                     esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "unifix-client init failed: %s (continuing anyway)",
                 esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Starting stream pipeline ...");
    stream_pipeline_start(stream_slot);

    /* Schedule the loading -> idle screen fade */
    esp_timer_create_args_t fade_args = {
        .callback = fade_to_idle_cb,
        .name = "screen_fade",
    };
    esp_timer_handle_t fade_timer = NULL;
    if (esp_timer_create(&fade_args, &fade_timer) == ESP_OK) {
        esp_timer_start_once(fade_timer, LOADING_TO_IDLE_DELAY_MS * 1000);
    }
}


/* ============================================================
 * WLAN
 * ============================================================ */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        scr_loading_set_status("Verbinde WLAN");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < 10) {
            s_retry++;
            char status[32];
            snprintf(status, sizeof(status), "Reconnect %d", s_retry);
            scr_loading_set_status(status);
            esp_wifi_connect();
        } else {
            scr_loading_set_status("Kein WLAN");
            ESP_LOGE(TAG, "WLAN connect failed after 10 retries");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_retry = 0;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        on_got_ip();
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    wifi_config_t wcfg = { 0 };
    strncpy((char *)wcfg.sta.ssid, WIFI_SSID, sizeof(wcfg.sta.ssid));
    strncpy((char *)wcfg.sta.password, WIFI_PASSWORD, sizeof(wcfg.sta.password));
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}


/* ============================================================
 * app_main
 * ============================================================ */
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_affinity = 1;
    lvgl_cfg.task_priority = 5;
    lvgl_cfg.task_stack = 8192;
    lvgl_cfg.timer_period_ms = 2;

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = lvgl_cfg,
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    /* Create + activate the loading screen first - replaces the
     * default white LVGL background immediately. */
    if (bsp_display_lock(200)) {
        scr_loading_create();
        bsp_display_unlock();
    }

    /* Setup mode check before WLAN */
    if (!device_token_has()) {
        ESP_LOGW(TAG, "No device_token in NVS, entering setup mode");
        scr_loading_set_status("Setup-Modus");
        setup_mode_run();
        return;
    }

    ESP_LOGI(TAG, "Device token present, starting normal boot");
    scr_loading_set_status("Starte");
    wifi_init();
}
