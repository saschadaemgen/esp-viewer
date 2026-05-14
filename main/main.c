/*
 * main.c - KOENIGSLIGA UniFi-Display
 *
 * ESP-Saison 2 Tag 1: nach Refactoring
 *
 * Aufgaben dieser Datei:
 *   - app_main() Entry: NVS, LVGL, Display-Init
 *   - WLAN-Initialisierung (wifi_init, wifi_event_handler)
 *   - Erste UI (ui_init, status_set)
 *
 * Was NICHT mehr hier ist (extrahiert):
 *   - MJPEG-Stream-Pipeline -> stream_pipeline.c und .h
 *
 * Saison 2 Spaeter wird hier weiter aufgeraeumt:
 *   - WLAN-Code -> wifi.c (wenn Setup-Mode dazukommt)
 *   - UI-Code   -> ui/-Files (wenn FSM-Screens kommen)
 */

#include <string.h>
#include <stdio.h>

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#include "stream_pipeline.h"

static const char *TAG = "KOENIG";

#define WIFI_SSID       "SONCLOUD"
#define WIFI_PASSWORD   "MKwlan1d250e-mured5"

static int s_retry = 0;
static lv_obj_t *s_status_label = NULL;

static void status_set(const char *fmt, ...)
{
    if (!s_status_label) return;
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (bsp_display_lock(100)) {
        lv_label_set_text(s_status_label, buf);
        bsp_display_unlock();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        status_set("Verbinde mit %s ...", WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < 10) {
            s_retry++;
            status_set("Reconnect %d ...", s_retry);
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_retry = 0;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        stream_pipeline_start();
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

static void ui_init(void)
{
    if (!bsp_display_lock(0)) return;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "KOENIGSLIGA");
    lv_obj_set_style_text_color(title, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "MJPEG Stream Receiver");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 150);

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Initialisiere ...");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_22, 0);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 0);
    bsp_display_unlock();
}

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

    ui_init();
    status_set("Starte WLAN ...");
    wifi_init();
}
