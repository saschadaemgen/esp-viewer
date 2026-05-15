/*
 * main.c - KOENIGSLIGA UniFi-Display
 *
 * ESP-Saison 2: Loading-Screen v2
 *
 * Architecture (FIXED after dangling-pointer crash):
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
 *      - status = "Lade Stream"
 *      - unifix_client_init + heartbeat
 *      - SSE listener
 *      - stream_pipeline_start(stream_slot)
 *      - 500ms later: lv_screen_load_anim(idle_screen, FADE, 400ms)
 *        LVGL fades loading out and idle in. No code in our hands.
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

static int s_retry = 0;
static lv_obj_t *s_ringing_overlay = NULL;
static lv_obj_t *s_idle_screen     = NULL;


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
        if (s_ringing_overlay && bsp_display_lock(50)) {
            lv_obj_clear_flag(s_ringing_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_ringing_overlay);
            bsp_display_unlock();
        }
        return;
    }

    if (strcmp(event_name, "doorbell.cancel") == 0) {
        ESP_LOGW(TAG, "[SSE] <<< DOORBELL CANCEL >>> %s", data);
        if (s_ringing_overlay && bsp_display_lock(50)) {
            lv_obj_add_flag(s_ringing_overlay, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();
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
 *            start stream + SSE, then schedule the fade.
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

        bsp_display_unlock();
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
