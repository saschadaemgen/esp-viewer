/*
 * KOENIGSLIGA UniFi-Display - Saison 3 Stage D-MJPEG
 * MJPEG-Stream vom RPi go2rtc (HTTP)
 */
#include <string.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "driver/jpeg_decode.h"

static const char *TAG = "KOENIG";
#define WIFI_SSID       "SONCLOUD"
#define WIFI_PASSWORD   "MKwlan1d250e-mured5"
#define MJPEG_HOST      "192.168.1.42"
#define MJPEG_PORT      1984
#define MJPEG_PATH      "/api/stream.mjpeg?src=intercom_mjpeg"

static int s_retry = 0;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_video_canvas = NULL;

static uint8_t *s_jpeg_buf = NULL;
static size_t s_jpeg_buf_capacity = 1024 * 1024;
static uint8_t *s_canvas_buf = NULL;
static size_t s_canvas_size = 0;
static jpeg_decoder_handle_t s_jpeg_engine = NULL;

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

static void mjpeg_task(void *arg)
{
    ESP_LOGI(TAG, "MJPEG task starting");

    jpeg_decode_memory_alloc_cfg_t mc_in = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };
    size_t jpeg_actual = 0;
    s_jpeg_buf = (uint8_t *)jpeg_alloc_decoder_mem(s_jpeg_buf_capacity, &mc_in, &jpeg_actual);
    s_jpeg_buf_capacity = jpeg_actual;
    ESP_LOGI(TAG, "JPEG buffer: %d bytes", (int)jpeg_actual);

    s_canvas_size = 800 * 1280 * 2;
    size_t cl = 64;
    s_canvas_size = (s_canvas_size + cl - 1) & ~(cl - 1);
    jpeg_decode_memory_alloc_cfg_t mc_out = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t cv_actual = 0;
    s_canvas_buf = (uint8_t *)jpeg_alloc_decoder_mem(s_canvas_size, &mc_out, &cv_actual);
    s_canvas_size = cv_actual;
    memset(s_canvas_buf, 0, s_canvas_size);
    ESP_LOGI(TAG, "Canvas: %d bytes", (int)cv_actual);

    jpeg_decode_engine_cfg_t engine_cfg = {
        .timeout_ms = 5000,
    };
    if (jpeg_new_decoder_engine(&engine_cfg, &s_jpeg_engine) != ESP_OK) {
        ESP_LOGE(TAG, "JPEG engine failed");
        vTaskDelete(NULL);
        return;
    }

    if (bsp_display_lock(100)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_clean(scr);
        s_status_label = NULL;
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        s_video_canvas = lv_canvas_create(scr);
        lv_canvas_set_buffer(s_video_canvas, s_canvas_buf, 800, 1280, LV_COLOR_FORMAT_RGB565);
        lv_obj_align(s_video_canvas, LV_ALIGN_CENTER, 0, 0);
        bsp_display_unlock();
    }

    size_t recv_buf_size = 32 * 1024;
    uint8_t *recv_buf = heap_caps_malloc(recv_buf_size, MALLOC_CAP_SPIRAM);
    if (!recv_buf) {
        ESP_LOGE(TAG, "recv_buf alloc failed");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        ESP_LOGI(TAG, "Connecting to RPi %s:%d", MJPEG_HOST, MJPEG_PORT);

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket failed");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        struct sockaddr_in dest = { 0 };
        dest.sin_family = AF_INET;
        dest.sin_port = htons(MJPEG_PORT);
        dest.sin_addr.s_addr = inet_addr(MJPEG_HOST);

        if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
            ESP_LOGE(TAG, "connect failed");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        char req[512];
        int req_len = snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "User-Agent: ESP32-P4-Display\r\n"
            "Accept: multipart/x-mixed-replace\r\n"
            "Connection: keep-alive\r\n"
            "\r\n",
            MJPEG_PATH, MJPEG_HOST, MJPEG_PORT);
        if (send(sock, req, req_len, 0) != req_len) {
            ESP_LOGE(TAG, "send failed");
            close(sock);
            continue;
        }
        ESP_LOGI(TAG, "GET sent, waiting for stream");

        size_t buf_len = 0;
        bool header_done = false;
        char boundary[64] = "";

        while (!header_done && buf_len < recv_buf_size - 1) {
            int n = recv(sock, recv_buf + buf_len, recv_buf_size - buf_len - 1, 0);
            if (n <= 0) break;
            buf_len += n;
            recv_buf[buf_len] = 0;
            char *header_end = strstr((char *)recv_buf, "\r\n\r\n");
            if (header_end) {
                header_done = true;
                char *bp = strstr((char *)recv_buf, "boundary=");
                if (bp) {
                    bp += 9;
                    int i = 0;
                    while (*bp && *bp != '\r' && *bp != '\n' && *bp != ';' && i < 63) {
                        boundary[i++] = *bp++;
                    }
                    boundary[i] = 0;
                }
                ESP_LOGI(TAG, "Boundary: '%s'", boundary);
                size_t header_size = (header_end - (char *)recv_buf) + 4;
                size_t leftover = buf_len - header_size;
                if (leftover > 0) {
                    memmove(recv_buf, recv_buf + header_size, leftover);
                }
                buf_len = leftover;
            }
        }

        if (!header_done || boundary[0] == 0) {
            ESP_LOGE(TAG, "Header parsing failed");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        int frame_count = 0;
        int64_t start_us = esp_timer_get_time();
        int64_t last_log_us = start_us;
        int last_dec_ms = 0;
        int last_jpeg_size = 0;

        while (1) {
            char marker[80];
            snprintf(marker, sizeof(marker), "--%s", boundary);
            size_t marker_len = strlen(marker);

            char *bpos = NULL;
            while (1) {
                bpos = (char *)memmem(recv_buf, buf_len, marker, marker_len);
                if (bpos) break;
                if (buf_len > recv_buf_size - 4096) {
                    memmove(recv_buf, recv_buf + buf_len - 256, 256);
                    buf_len = 256;
                }
                int n = recv(sock, recv_buf + buf_len, recv_buf_size - buf_len, 0);
                if (n <= 0) goto reconnect;
                buf_len += n;
            }

            size_t skip = (bpos - (char *)recv_buf) + marker_len;
            while (skip + 2 > buf_len) {
                int n = recv(sock, recv_buf + buf_len, recv_buf_size - buf_len, 0);
                if (n <= 0) goto reconnect;
                buf_len += n;
            }
            if (recv_buf[skip] == '\r') skip++;
            if (recv_buf[skip] == '\n') skip++;

            int content_length = -1;
            while (1) {
                char *he = (char *)memmem(recv_buf + skip, buf_len - skip, "\r\n\r\n", 4);
                if (he) {
                    char tmp = *he;
                    *he = 0;
                    char *cl = strstr((char *)recv_buf + skip, "Content-Length:");
                    if (cl) {
                        cl += 15;
                        while (*cl == ' ') cl++;
                        content_length = atoi(cl);
                    }
                    *he = tmp;
                    skip = (he - (char *)recv_buf) + 4;
                    break;
                }
                int n = recv(sock, recv_buf + buf_len, recv_buf_size - buf_len, 0);
                if (n <= 0) goto reconnect;
                buf_len += n;
            }

            if (content_length <= 0 || content_length > (int)s_jpeg_buf_capacity) {
                ESP_LOGW(TAG, "Bad content-length: %d", content_length);
                goto reconnect;
            }

            size_t already = buf_len - skip;
            if (already > (size_t)content_length) already = content_length;
            memcpy(s_jpeg_buf, recv_buf + skip, already);

            size_t need = content_length - already;
            size_t got = already;
            while (need > 0) {
                int n = recv(sock, s_jpeg_buf + got, need, 0);
                if (n <= 0) goto reconnect;
                got += n;
                need -= n;
            }

            size_t consumed = skip + content_length;
            if (consumed < buf_len) {
                size_t rest = buf_len - consumed;
                memmove(recv_buf, recv_buf + consumed, rest);
                buf_len = rest;
            } else {
                buf_len = 0;
            }

            jpeg_decode_cfg_t dec_cfg = {
                .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
            };
            uint32_t out_size = 0;
            int64_t t_dec_start = esp_timer_get_time();
            esp_err_t ret = jpeg_decoder_process(s_jpeg_engine, &dec_cfg,
                s_jpeg_buf, content_length,
                s_canvas_buf, s_canvas_size, &out_size);
            int64_t t_dec_end = esp_timer_get_time();

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Decode failed: %s, size=%d", esp_err_to_name(ret), content_length);
                continue;
            }

            last_dec_ms = (int)((t_dec_end - t_dec_start) / 1000);
            last_jpeg_size = content_length;

            if (bsp_display_lock(20)) {
                lv_obj_invalidate(s_video_canvas);
                bsp_display_unlock();
            }

            frame_count++;

            int64_t now_us = esp_timer_get_time();
            if (now_us - last_log_us > 1000000) {
                float secs = (now_us - start_us) / 1000000.0;
                float fps = frame_count / secs;
                ESP_LOGI(TAG, "fps=%.1f  dec=%dms  frames=%d  jpeg=%d KB",
                    fps, last_dec_ms, frame_count, last_jpeg_size / 1024);
                last_log_us = now_us;
            }
        }
reconnect:
        ESP_LOGW(TAG, "Stream lost, reconnecting...");
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(2000));
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
        xTaskCreatePinnedToCore(mjpeg_task, "mjpeg", 16384, NULL, 5, NULL, 0);
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
