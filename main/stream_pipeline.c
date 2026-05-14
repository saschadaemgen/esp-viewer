/*
 * stream_pipeline.c - MJPEG-Stream-Receiver Pipeline
 *
 * KOENIGSLIGA UniFi-Display, ESP-Saison 2 Tag 1
 *
 * Extrahiert aus main.c (Saison 1, 10. Mai 2026, 11 fps stabil).
 * REINES REFACTORING - keine Logik-Aenderung.
 *
 * Verifiziert:
 *   - 23.500+ Frames stabil ueber 36+ Minuten in Saison 1
 *   - Hardware-JPEG-Decoder dec=10ms pro Frame
 *   - RGB565 BGR-Order korrekt
 *   - LVGL Triple-Buffer + Avoid-Tear
 */

#include "stream_pipeline.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "driver/jpeg_decode.h"

static const char *TAG = "STREAM";

/*
 * Stream-Endpoint-Konstanten
 * Saison 2 Spaeter: kommen aus /esp/config vom unifix-Server.
 */
#define MJPEG_HOST      "192.168.1.42"
#define MJPEG_PORT      1984
#define MJPEG_PATH      "/api/stream.mjpeg?src=intercom_mjpeg"

/* JPEG/Canvas-Buffer */
static uint8_t *s_jpeg_buf = NULL;
static size_t s_jpeg_buf_capacity = 1024 * 1024;

static uint8_t *s_canvas_buf = NULL;
static size_t s_canvas_size = 0;

static jpeg_decoder_handle_t s_jpeg_engine = NULL;
static lv_obj_t *s_video_canvas = NULL;

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

void stream_pipeline_start(void)
{
    xTaskCreatePinnedToCore(mjpeg_task, "mjpeg", 16384, NULL, 5, NULL, 0);
}
