/*
 * stream_pipeline.c - MJPEG-Stream-Receiver Pipeline
 *
 * KOENIGSLIGA UniFi-Display
 *
 * S5-04 Direct-FB-Pfad (lv_canvas final entfernt):
 *   - JPEG decoder output RGB565 (matched DPI-FB-Format)
 *   - 2 MB PSRAM-Buffer, 64B-aligned via jpeg_alloc_decoder_mem
 *   - esp_lcd_panel_draw_bitmap schreibt direkt in die stream_view-
 *     Region des Panel-FB. Pointer-Swap, kein Kopieren.
 *   - Draw-Gate s_stream_visible: nur im Livestream-/Ringing-View
 *     wird das Panel geschrieben. Decoder laeuft sonst weiter um den
 *     TCP-Backlog leer zu halten.
 *
 * Hintergrund: der lv_canvas-Pfad (Saison 1-3 inkl. S5-03) war mit
 * 800x1280 RGB-Buffer + lv_obj_invalidate pro Frame ein CPU-Wuerger
 * (~49% lvglDraw, Bild ruckelig - Geraete-Messung S5-03). Direct-FB
 * via DMA2D-Copy ist praktisch gratis (~10% CPU, 60 fps - S5-02).
 *
 * v5: aggressive backlog skip. Nach jedem Frame: solange noch
 *     Bytes im TCP-Buffer warten und der naechste Frame schon
 *     komplett empfangbar ist, skippe ohne Decode. ESP entscheidet
 *     pro Frame neu, decodiert nur den AKTUELLSTEN Frame im Buffer.
 */

#include "stream_pipeline.h"

#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>

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

#include "freertos/semphr.h"

#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "driver/jpeg_decode.h"
#include "hal/color_types.h"

/*
 * S5-07 forward-declarations: lokale Patches/private-APIs.
 *
 * lvgl_port_get_trans_sem: lokaler Patch in
 *   managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c
 *   (exposiert lvgl-port's internes trans_sem). Nicht VCS-tracked.
 *
 * esp_async_fbcpy_*: ESP-IDF DMA2D-FB-Copy-API, deklariert in
 *   components/esp_lcd/priv_include/esp_async_fbcpy.h. priv_include
 *   ist nicht von ausserhalb components/esp_lcd erreichbar; wir
 *   replizieren die noetigen Typen + Funktions-Deklarationen hier.
 *   Die Symbole selbst sind extern (nicht static), Linker findet sie.
 *   Bei ESP-IDF-Update muss der Header-Vergleich gemacht werden.
 *
 * Pattern wie bei bsp_lcd_get_panel_handle (S4-10) - klar dokumentiert,
 * lokal patch-frei (nur Forward-Decl), zentral hier in der einen Datei.
 */
extern SemaphoreHandle_t lvgl_port_get_trans_sem(lv_display_t *disp);

typedef struct esp_async_fbcpy_context_t *esp_async_fbcpy_handle_t;
typedef struct {
    /* keine Felder im aktuellen IDF, aber API verlangt const-Pointer */
} esp_async_fbcpy_config_t;
typedef struct {
    const void *src_buffer;
    void *dst_buffer;
    size_t src_buffer_size_x;
    size_t src_buffer_size_y;
    size_t dst_buffer_size_x;
    size_t dst_buffer_size_y;
    size_t src_offset_x;
    size_t src_offset_y;
    size_t dst_offset_x;
    size_t dst_offset_y;
    size_t copy_size_x;
    size_t copy_size_y;
    color_space_pixel_format_t pixel_format_unique_id;
} esp_async_fbcpy_trans_desc_t;
typedef struct {
    /* keine Felder im aktuellen IDF */
} esp_async_fbcpy_event_data_t;
typedef bool (*esp_async_fbcpy_event_callback_t)(esp_async_fbcpy_handle_t mcp,
                                                   esp_async_fbcpy_event_data_t *event_data,
                                                   void *cb_args);
extern esp_err_t esp_async_fbcpy_install(const esp_async_fbcpy_config_t *config,
                                          esp_async_fbcpy_handle_t *mcp);
extern esp_err_t esp_async_fbcpy(esp_async_fbcpy_handle_t mcp,
                                  esp_async_fbcpy_trans_desc_t *transaction,
                                  esp_async_fbcpy_event_callback_t memcpy_done_cb,
                                  void *cb_args);

static const char *TAG = "STREAM";

/*
 * Stream-Endpoint (S5-06):
 *
 * Direkt-Server (auth-frei, Port 8555) statt 9080-carvilon-Proxy.
 * Mess-Phase-Entscheidung aus S5-03 bleibt: ESP zieht vom Stream-
 * Server direkt, nicht ueber den carvilon-Proxy.
 *
 * Seit S5-06: Stream laeuft auf dem RPi statt auf dem Windows-Desktop.
 * .42 ist auch der carvilon-Host (Port 9080, SSE) - Stream-Server
 * hoert separat auf 8555 auf demselben Host, beide Ports koexistieren.
 * Profil mjpeg_bal liefert 800x1280 @12fps q:v6 - matched genau die
 * Decoder-Aufloesung, keine Skalierung noetig.
 *
 * KEIN Bearer-Header: dieser Endpoint hat keine Auth-Pruefung.
 *
 * Server-COM-Marker ist serverseitig bereits gefixt.
 */
#define MJPEG_HOST      "192.168.1.42"
#define MJPEG_PORT      8555
#define MJPEG_PATH      "/api/stream.mjpeg?src=mjpeg_bal"

/*
 * S5-04 Direct-FB Geometrie:
 *
 * Display Portrait 800 x 1280 (BSP_LCD_H_RES x BSP_LCD_V_RES).
 *
 * Stream-Region innerhalb des scr_idle-Layouts (siehe ui_tokens.h +
 * main/ui/scr_idle.c):
 *   y =    0..14    UI_SCREEN_PAD       (screen-bg, kein Stream)
 *   y =   14..78    UI_TOPBAR_H=64      (LVGL Topbar, kein Stream)
 *   y =   78..88    UI_SCREEN_GAP=10
 *   y =   88..1160  stream_view 1072    <-- Stream-Region (1072 Zeilen)
 *   y = 1160..1170  UI_SCREEN_GAP=10
 *   y = 1170..1266  UI_ACTIONS_H=96     (LVGL Action-Bar, kein Stream)
 *   y = 1266..1280  UI_SCREEN_PAD
 *
 * Horizontal (Default, cropped/Idle-Mode, S5-11):
 *   x =   0..14   UI_SCREEN_PAD (LVGL Design-Frame-Border links)
 *   x =  14..786  Stream-Region (772 Pixel breit)
 *   x = 786..800  UI_SCREEN_PAD (LVGL Design-Frame-Border rechts)
 * Bis S5-10 war x voll-breit (0..800), was die seitlichen Design-Frame-
 * Border-Pixel beim Stream-Copy uebermalte - LVGL muesste sie pro Frame
 * neu malen (machte es nicht). Seit S5-11 cropt Stream den 14px-Pad
 * weg, Border bleibt sichtbar.
 *
 * Im fullscreen-Mode (Klingel, S5-10) wird die x-Crop UEBERSPRUNGEN -
 * dort darf Stream den ganzen Display fuellen weil die Idle-Chrome eh
 * versteckt ist.
 *
 * Source-Pointer-Offset fuer vertical-crop des 800x1280-Decoder-Buffers:
 *   STREAM_SRC_ROW0 = (1280 - 1072) / 2 = 104  (zentrierter v-crop)
 *   STREAM_SRC_OFFSET = 104 * 800 * 2 = 166400 Bytes
 *
 * Decoder schreibt das volle 800x1280 in s_stream_buf. fbcpy liest
 * den jeweils richtigen Sub-Bereich (siehe fs/!fs-Branching in mjpeg).
 */
#define STREAM_W           800
#define STREAM_H           1280
#define STREAM_BPP         2   /* RGB565 = 2 Bytes/Pixel */
#define STREAM_Y_TOP       88
#define STREAM_Y_BOTTOM    1160
#define STREAM_REGION_H    (STREAM_Y_BOTTOM - STREAM_Y_TOP)        /* 1072 */
#define STREAM_SRC_ROW0    ((STREAM_H - STREAM_REGION_H) / 2)      /* 104 */
#define STREAM_SRC_OFFSET  (STREAM_SRC_ROW0 * STREAM_W * STREAM_BPP) /* 166400 */
#define STREAM_X_PAD       14   /* UI_SCREEN_PAD: Idle-Seiten-Border */
#define STREAM_REGION_W    (STREAM_W - 2 * STREAM_X_PAD)           /* 772 */

/* S5-16/S5-18 Klingel-Mode: Stream Vollbreite x=0..800, mit Header
 * oben + Toolbar unten in den sicheren UI-Bereichen (vom Stream nicht
 * beschrieben). Single Source of Truth ist ui_tokens.h - hier doppelt
 * deklariert weil stream_pipeline.c keine UI-Tokens included. Bei
 * Aenderung beide Werte synchronisieren.
 *
 *   y =    0 ..   88   KLINGEL_HEADER_H  (LVGL Klingel-Header-Bar)
 *   y =   88 .. 1140   Stream-Video      (KLINGEL_VIDEO_H = 1052)
 *   y = 1140 .. 1280   KLINGEL_TOOLBAR_H (LVGL Klingel-Toolbar)
 *
 * Video wird zentriert v-cropt: aus dem 800x1280 Decoder-Buffer kopieren
 * wir die mittleren 1052 Zeilen (KLINGEL_SRC_ROW0=114) in den FB ab y=88. */
#define KLINGEL_HEADER_H   88
#define KLINGEL_TOOLBAR_H  140
#define KLINGEL_VIDEO_H    (STREAM_H - KLINGEL_HEADER_H - KLINGEL_TOOLBAR_H)  /* 1052 */
#define KLINGEL_SRC_ROW0   ((STREAM_H - KLINGEL_VIDEO_H) / 2)                /* 114 */

/* JPEG input + decode-output buffers */
static uint8_t *s_jpeg_buf = NULL;
static size_t s_jpeg_buf_capacity = 1024 * 1024;

static uint8_t *s_stream_buf = NULL;
static size_t   s_stream_buf_size = 0;

static jpeg_decoder_handle_t s_jpeg_engine = NULL;

/*
 * Draw-Gate. Default false bei Boot (scr_loading aktiv, kein Stream
 * sichtbar). Wird per stream_pipeline_set_visible von scr_idle /
 * scr_ringing umgeschaltet sobald sich der View-State aendert.
 * volatile damit der Compiler den Frame-Loop-Read nicht wegoptimiert.
 */
static volatile bool s_stream_visible = false;

/*
 * S5-10: Vollbild-Toggle. Default false = Stream-Region (y=88..1160)
 * wie im Idle. True = ganzes FB (y=0..1280) fuer Klingel-Vollbild.
 * Caller muss Topbar/Action-Bar vorher verstecken sonst flickert es.
 */
static volatile bool s_stream_fullscreen = false;

void stream_pipeline_set_visible(bool visible)
{
    if (s_stream_visible != visible) {
        ESP_LOGI(TAG, "stream visible: %d -> %d", s_stream_visible, visible);
        s_stream_visible = visible;
    }
}

void stream_pipeline_set_fullscreen(bool fullscreen)
{
    if (s_stream_fullscreen != fullscreen) {
        ESP_LOGI(TAG, "stream fullscreen: %d -> %d", s_stream_fullscreen, fullscreen);
        s_stream_fullscreen = fullscreen;
    }
}

/*
 * S5-07 FB-Sync-Setup (Variante B2):
 *
 * Mit num_fbs=2 + AVOID_TEAR alterniert die Hardware-DMA zwischen
 * fbs[0] und fbs[1] beim VSYNC. esp_lvgl_port nutzt diese beiden FBs
 * als LVGL-Buffers (Zero-Copy + cur_fb_index-Update).
 *
 * Unser Stream muss in BEIDE FBs die Stream-Region schreiben, sonst
 * zeigt der gerade nicht-aktualisierte FB einen alten Frame und beim
 * Display-Rotate sieht der User Mehrfach-Hand (Geist-Symptom).
 *
 * Lösung: nach jedem Decode 2x esp_async_fbcpy (DMA2D-Copy aus
 * s_stream_buf in fbs[0]+offset bzw. fbs[1]+offset). Beide Copies
 * sequentiell um die DMA2D-Engine nicht ueberladen zu lassen.
 *
 *   s_fbcpy_handle    Eigener DMA2D-Channel (esp_async_fbcpy_install).
 *   s_dpi_fbs[2]      FB-Pointer von esp_lcd_dpi_panel_get_frame_buffer.
 *   s_copy_done_sem   Binary, gegeben im Done-Callback. mjpeg_task
 *                     wartet pro Copy darauf.
 *   s_lvgl_trans_sem  Cached lvgl-port-trans_sem (siehe S5-06).
 *                     on_refresh_done-Wrapper triggert es weiter.
 */
static esp_async_fbcpy_handle_t s_fbcpy_handle    = NULL;
static void                    *s_dpi_fbs[2]      = { NULL, NULL };
static SemaphoreHandle_t        s_copy_done_sem   = NULL;
static SemaphoreHandle_t        s_lvgl_trans_sem  = NULL;

static IRAM_ATTR bool stream_copy_done_cb(esp_async_fbcpy_handle_t mcp,
                                            esp_async_fbcpy_event_data_t *evt,
                                            void *cb_args)
{
    (void)mcp; (void)evt; (void)cb_args;
    BaseType_t hp_woken = pdFALSE;
    if (s_copy_done_sem) {
        xSemaphoreGiveFromISR(s_copy_done_sem, &hp_woken);
    }
    return (hp_woken == pdTRUE);
}

static IRAM_ATTR bool stream_vsync_fwd_cb(esp_lcd_panel_handle_t panel,
                                           esp_lcd_dpi_panel_event_data_t *edata,
                                           void *user_ctx)
{
    (void)panel; (void)edata; (void)user_ctx;
    BaseType_t hp_woken = pdFALSE;
    /* Forward zu esp_lvgl_port's internem trans_sem - sonst haengt
     * lvgl_port_flush_callback im DSI/avoid_tearing-Pfad ewig. */
    if (s_lvgl_trans_sem) {
        xSemaphoreGiveFromISR(s_lvgl_trans_sem, &hp_woken);
    }
    return (hp_woken == pdTRUE);
}

esp_err_t stream_pipeline_install_fb_sync(void)
{
    if (s_fbcpy_handle) {
        ESP_LOGW(TAG, "fb sync already installed");
        return ESP_ERR_INVALID_STATE;
    }

    /* DMA2D-Channel installieren fuer unsere async_fbcpy-Calls. */
    esp_async_fbcpy_config_t fbcpy_cfg = {0};
    esp_err_t err = esp_async_fbcpy_install(&fbcpy_cfg, &s_fbcpy_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_async_fbcpy_install failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Panel-Handle aus BSP. */
    esp_lcd_panel_handle_t panel = bsp_lcd_get_panel_handle();
    if (!panel) {
        ESP_LOGE(TAG, "panel handle NULL");
        return ESP_ERR_INVALID_STATE;
    }

    /* Beide DPI-FB-Adressen holen. Mit num_fbs=2 in sdkconfig sind das
     * exakt die zwei FBs die LVGL alterniert beschreibt + die die
     * Hardware-DMA rotiert anzeigt. */
    err = esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &s_dpi_fbs[0], &s_dpi_fbs[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_frame_buffer failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DPI FBs: fb0=%p, fb1=%p", s_dpi_fbs[0], s_dpi_fbs[1]);

    /* lvgl-port's internes trans_sem cachen fuer den on_refresh_done-
     * Forward (siehe S5-06 Begruendung). */
    lv_display_t *disp = lv_disp_get_default();
    if (!disp) {
        ESP_LOGE(TAG, "lv_disp_get_default returned NULL");
        return ESP_ERR_INVALID_STATE;
    }
    s_lvgl_trans_sem = lvgl_port_get_trans_sem(disp);
    if (!s_lvgl_trans_sem) {
        ESP_LOGW(TAG, "lvgl_port trans_sem is NULL - avoid_tearing not active?");
    }

    /* Copy-Done-Sema fuer den mjpeg_task. Binary - wir warten pro Copy
     * darauf, kein Backlog. Initial NICHT gegeben, der erste Take wird
     * nach dem ersten async_fbcpy ausgeloest. */
    s_copy_done_sem = xSemaphoreCreateBinary();
    if (!s_copy_done_sem) {
        ESP_LOGE(TAG, "copy_done sema alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* on_refresh_done-Forwarder registrieren. KEIN on_color_trans_done -
     * wir nutzen esp_lcd_panel_draw_bitmap nicht mehr (statt dessen
     * esp_async_fbcpy direkt), also brauchen wir den Color-Trans-Done
     * nicht. Aber der on_refresh_done MUSS dranbleiben fuer LVGL. */
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = NULL,
        .on_refresh_done     = stream_vsync_fwd_cb,
    };
    err = esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, disp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register panel callbacks failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "FB sync installed: fbcpy handle=%p, copy_done_sem + lvgl trans_sem forward",
             s_fbcpy_handle);
    return ESP_OK;
}

/*
 * Parse one MJPEG frame from the stream and write the JPEG payload
 * into out_jpeg. Returns the content_length on success or -1 on error.
 * Reads from socket as needed. recv_buf/buf_len are the persistent
 * receive buffer state.
 */
static int read_next_frame(int sock, uint8_t *recv_buf, size_t recv_buf_size,
                           size_t *buf_len_io, const char *boundary,
                           uint8_t *out_jpeg, size_t out_jpeg_cap)
{
    size_t buf_len = *buf_len_io;

    char marker[80];
    snprintf(marker, sizeof(marker), "--%s", boundary);
    size_t marker_len = strlen(marker);

    /* Find the boundary marker */
    char *bpos = NULL;
    while (1) {
        bpos = (char *)memmem(recv_buf, buf_len, marker, marker_len);
        if (bpos) break;
        if (buf_len > recv_buf_size - 4096) {
            memmove(recv_buf, recv_buf + buf_len - 256, 256);
            buf_len = 256;
        }
        int n = recv(sock, recv_buf + buf_len, recv_buf_size - buf_len, 0);
        if (n <= 0) { *buf_len_io = buf_len; return -1; }
        buf_len += n;
    }

    size_t skip = (bpos - (char *)recv_buf) + marker_len;
    while (skip + 2 > buf_len) {
        int n = recv(sock, recv_buf + buf_len, recv_buf_size - buf_len, 0);
        if (n <= 0) { *buf_len_io = buf_len; return -1; }
        buf_len += n;
    }
    if (recv_buf[skip] == '\r') skip++;
    if (recv_buf[skip] == '\n') skip++;

    /* Parse Content-Length header */
    int content_length = -1;
    while (1) {
        char *he = (char *)memmem(recv_buf + skip, buf_len - skip, "\r\n\r\n", 4);
        if (he) {
            char tmp = *he;
            *he = 0;
            char *cl_str = strstr((char *)recv_buf + skip, "Content-Length:");
            if (cl_str) {
                cl_str += 15;
                while (*cl_str == ' ') cl_str++;
                content_length = atoi(cl_str);
            }
            *he = tmp;
            skip = (he - (char *)recv_buf) + 4;
            break;
        }
        int n = recv(sock, recv_buf + buf_len, recv_buf_size - buf_len, 0);
        if (n <= 0) { *buf_len_io = buf_len; return -1; }
        buf_len += n;
    }

    if (content_length <= 0 || content_length > (int)out_jpeg_cap) {
        ESP_LOGW(TAG, "Bad content-length: %d", content_length);
        *buf_len_io = buf_len;
        return -1;
    }

    /* Copy whatever JPEG bytes are already in recv_buf */
    size_t already = buf_len - skip;
    if (already > (size_t)content_length) already = content_length;
    memcpy(out_jpeg, recv_buf + skip, already);

    /* Receive the rest directly into out_jpeg */
    size_t need = content_length - already;
    size_t got = already;
    while (need > 0) {
        int n = recv(sock, out_jpeg + got, need, 0);
        if (n <= 0) { *buf_len_io = buf_len; return -1; }
        got += n;
        need -= n;
    }

    /* Shift any leftover bytes (start of next frame) to front of recv_buf */
    size_t consumed = skip + content_length;
    if (consumed < buf_len) {
        size_t rest = buf_len - consumed;
        memmove(recv_buf, recv_buf + consumed, rest);
        buf_len = rest;
    } else {
        buf_len = 0;
    }

    *buf_len_io = buf_len;
    return content_length;
}

static void mjpeg_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "MJPEG task starting (S5-04 Direct-FB render path)");

    /* Panel-Handle aus BSP (bsp_lcd_get_panel_handle wurde in S4-10
     * lokal in common_components/esp32_p4_function_ev_board.c
     * eingefuegt, NICHT VCS-tracked). Panel ist zum Task-Start
     * verfuegbar weil mjpeg_task erst nach bsp_display_start_with_config
     * in main.c gestartet wird. */
    esp_lcd_panel_handle_t panel = bsp_lcd_get_panel_handle();
    if (!panel) {
        ESP_LOGE(TAG, "panel handle is NULL - BSP not initialized?");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "got panel handle %p, stream region y=%d..%d (%d rows)",
             panel, STREAM_Y_TOP, STREAM_Y_BOTTOM, STREAM_REGION_H);

    jpeg_decode_memory_alloc_cfg_t mc_in = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };
    size_t jpeg_actual = 0;
    s_jpeg_buf = (uint8_t *)jpeg_alloc_decoder_mem(s_jpeg_buf_capacity, &mc_in, &jpeg_actual);
    s_jpeg_buf_capacity = jpeg_actual;
    ESP_LOGI(TAG, "JPEG buffer: %d bytes", (int)jpeg_actual);

    /* RGB565 2 Bytes/Pixel * 800 * 1280 = 2,048,000 Bytes pro Frame.
     * 64B-aligned aufrunden fuer DMA-Compatibility. */
    s_stream_buf_size = STREAM_W * STREAM_H * STREAM_BPP;
    const size_t align = 64;
    s_stream_buf_size = (s_stream_buf_size + align - 1) & ~(align - 1);

    jpeg_decode_memory_alloc_cfg_t mc_out = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t sv_actual = 0;
    s_stream_buf = (uint8_t *)jpeg_alloc_decoder_mem(s_stream_buf_size, &mc_out, &sv_actual);
    s_stream_buf_size = sv_actual;
    memset(s_stream_buf, 0, s_stream_buf_size);
    ESP_LOGI(TAG, "Stream buffer (RGB565): %d bytes", (int)sv_actual);

    jpeg_decode_engine_cfg_t engine_cfg = {
        .timeout_ms = 5000,
    };
    if (jpeg_new_decoder_engine(&engine_cfg, &s_jpeg_engine) != ESP_OK) {
        ESP_LOGE(TAG, "JPEG engine failed");
        vTaskDelete(NULL);
        return;
    }

    size_t recv_buf_size = 32 * 1024;
    uint8_t *recv_buf = heap_caps_malloc(recv_buf_size, MALLOC_CAP_SPIRAM);
    if (!recv_buf) {
        ESP_LOGE(TAG, "recv_buf alloc failed");
        vTaskDelete(NULL);
        return;
    }

    /* Scratch buffer to dump skipped frames into (so we can read+discard) */
    uint8_t *skip_buf = heap_caps_malloc(s_jpeg_buf_capacity, MALLOC_CAP_SPIRAM);
    if (!skip_buf) {
        ESP_LOGE(TAG, "skip_buf alloc failed");
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

        /* S5-03: KEIN Bearer-Header gegen den .187:8555-Direktserver.
         * Der frueher hier sitzende device_token_get-Aufruf + die
         * "Authorization: Bearer %s\r\n"-Zeile sind entfernt - der
         * Endpoint hat keine Auth-Pruefung. Wenn die Produktiv-Quelle
         * spaeter wieder der 9080-Proxy mit Bearer wird, kommt der
         * Block zurueck. */
        char req[768];
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
            memset(req, 0, sizeof(req));
            close(sock);
            continue;
        }
        memset(req, 0, sizeof(req));
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
        int skipped_count = 0;
        int64_t start_us = esp_timer_get_time();
        int64_t last_log_us = start_us;
        int last_dec_ms = 0;
        int last_jpeg_size = 0;

        while (1) {
            /* Read next frame into s_jpeg_buf */
            int content_length = read_next_frame(sock, recv_buf, recv_buf_size,
                                                  &buf_len, boundary,
                                                  s_jpeg_buf, s_jpeg_buf_capacity);
            if (content_length < 0) goto reconnect;

            /*
             * AGGRESSIVE BACKLOG SKIP:
             * Solange noch nennenswert Bytes im TCP-Buffer warten
             * (> 1/4 Frame), bedeutet das: ein weiterer Frame ist
             * teilweise oder ganz angekommen. Wir verwerfen den
             * gerade gelesenen Frame OHNE Decode und lesen den
             * naechsten. So saugt der ESP den Buffer leer bis nur
             * noch der aktuellste Frame uebrig ist.
             */
            int pending = 0;
            int skip_quarter = content_length / 4;
            while (ioctl(sock, FIONREAD, &pending) == 0 &&
                   pending > skip_quarter) {
                int n = read_next_frame(sock, recv_buf, recv_buf_size,
                                         &buf_len, boundary,
                                         skip_buf, s_jpeg_buf_capacity);
                if (n < 0) goto reconnect;
                skipped_count++;
                /* Copy newest skipped frame into s_jpeg_buf in case
                 * the loop ends and we decode this one. */
                memcpy(s_jpeg_buf, skip_buf, n);
                content_length = n;
            }

            /* Decode the latest frame.
             * Output RGB565 BGR -> matched DPI-FB-Format
             * (CONFIG_BSP_LCD_COLOR_FORMAT_RGB565). */
            jpeg_decode_cfg_t dec_cfg = {
                .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
            };
            uint32_t out_size = 0;
            int64_t t_dec_start = esp_timer_get_time();
            esp_err_t ret = jpeg_decoder_process(s_jpeg_engine, &dec_cfg,
                s_jpeg_buf, content_length,
                s_stream_buf, s_stream_buf_size, &out_size);
            int64_t t_dec_end = esp_timer_get_time();

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Decode failed: %s, size=%d", esp_err_to_name(ret), content_length);
                continue;
            }

            last_dec_ms = (int)((t_dec_end - t_dec_start) / 1000);
            last_jpeg_size = content_length;

            /* S5-04 Direct-FB Render-Pfad mit View-Gate.
             *
             * Wenn der Stream nicht sichtbar sein soll (Screensaver,
             * Settings, Loading): Draw skip - der Decoder lief gerade
             * durch um TCP-Backlog leer zu halten, aber das Bild geht
             * NICHT aufs Panel.
             *
             * Wenn sichtbar: esp_lcd_panel_draw_bitmap in die Stream-
             * Region. x=0..800 voll-breit (LVGL-Frame-Overlay aus
             * S5-04 Teil B holt die anthrazit Seitenraender + runde
             * Ecken zurueck). y=88..1160 (Topbar + Action-Bar bleiben
             * LVGL-exklusiv). Source-Offset zeigt auf row 104 des
             * decoded 800x1280-Frames - zentrierter vertical-crop ohne
             * memcpy, ohne Scaling.
             *
             * DMA2D-Copy im JD9365-Treiber (use_dma2d=true Default)
             * macht den Pixel-Transfer ohne CPU-Last.
             *
             * bsp_display_lock = lvgl_port_lock - serialisiert mit
             * LVGL-Flushes damit nicht zwei Tasks parallel ins esp_lcd-
             * API gehen. */
            if (!s_stream_visible) {
                frame_count++;
                int64_t now_us2 = esp_timer_get_time();
                if (now_us2 - last_log_us > 1000000) {
                    float secs = (now_us2 - start_us) / 1000000.0f;
                    float fps = frame_count / secs;
                    ESP_LOGI(TAG, "fps=%.1f  dec=%dms  frames=%d  skipped=%d  jpeg=%d KB  [HIDDEN]",
                        fps, last_dec_ms, frame_count, skipped_count, last_jpeg_size / 1024);
                    last_log_us = now_us2;
                }
                continue;
            }

            /* S5-07 B2: Stream-Region in BEIDE DPI-FBs schreiben via
             * esp_async_fbcpy. Damit ist egal welchen FB die Hardware-
             * Rotation gerade zeigt - beide haben den aktuellen Frame.
             *
             * Source: s_stream_buf (800x1280 RGB565 decoded), wir kopieren
             * die Stream-Region (zentrierter v-crop ab Row 104, 1072 Zeilen).
             *
             * Destination: jeweils s_dpi_fbs[0] / s_dpi_fbs[1] - direkt
             * die Pointer aus esp_lcd_dpi_panel_get_frame_buffer.
             * dst_offset_y = STREAM_Y_TOP=88 (in das Topbar/Actions-y
             * NICHT geschrieben wird, LVGL-Domaene bleibt).
             *
             * bsp_display_lock um beide Copies + Cache-Sync, damit LVGL
             * nicht gleichzeitig in den gleichen FB Zero-Copy-cache-msync
             * macht. DMA-Async aber sequentiell - DMA2D-Engine kann nur
             * eine Op zur Zeit. */
            if (!s_fbcpy_handle || !s_dpi_fbs[0] || !s_dpi_fbs[1] || !s_copy_done_sem) {
                ESP_LOGW(TAG, "fb sync not installed - skipping draw");
            } else if (bsp_display_lock(100)) {
                /*
                 * Copy-Geometrie haengt am s_stream_fullscreen-Toggle.
                 *
                 *   fullscreen=false (S5-11 Default, Idle):
                 *     X: 14..786 (772 wide, innere Region, LVGL Frame-Border
                 *                 links + rechts bleibt sichtbar)
                 *     Y: 88..1160 (1072 hoch, Stream-Window, LVGL Topbar +
                 *                  Action-Bar bleiben sichtbar)
                 *   fullscreen=true (S5-18 Klingel):
                 *     X: 0..800   (voll-breit)
                 *     Y: 88..1140 (KLINGEL_VIDEO_H=1052 hoch, zwischen
                 *                  oberer Klingel-Header-Bar [0..88] und
                 *                  unterer Klingel-Toolbar [1140..1280])
                 *     Header- + Toolbar-Bereich NICHT vom Stream beschrieben
                 *     - dort liegt die LVGL-Klingel-UI im "sicheren" Bereich,
                 *     kein Doppel-Render-Konflikt.
                 *
                 * Snapshot des Toggle in lokale Variable damit ein gleichzeitiger
                 * set_fullscreen-Call (anderer Task) nicht mitten in der
                 * Frame-Bearbeitung die Geometrie unter uns aendert.
                 */
                const bool   fs          = s_stream_fullscreen;
                const size_t copy_src_x  = fs ? 0u                       : (size_t)STREAM_X_PAD;
                const size_t copy_src_y  = fs ? (size_t)KLINGEL_SRC_ROW0 : (size_t)STREAM_SRC_ROW0;
                const size_t copy_dst_x  = fs ? 0u                       : (size_t)STREAM_X_PAD;
                const size_t copy_dst_y  = fs ? (size_t)KLINGEL_HEADER_H : (size_t)STREAM_Y_TOP;
                const size_t copy_width  = fs ? (size_t)STREAM_W         : (size_t)STREAM_REGION_W;
                const size_t copy_height = fs ? (size_t)KLINGEL_VIDEO_H  : (size_t)STREAM_REGION_H;

                esp_async_fbcpy_trans_desc_t cfg = {
                    .src_buffer = s_stream_buf,
                    .src_buffer_size_x = STREAM_W,
                    .src_buffer_size_y = STREAM_H,
                    .src_offset_x = copy_src_x,
                    .src_offset_y = copy_src_y,
                    .dst_buffer_size_x = STREAM_W,           /* FB ist 800x1280 */
                    .dst_buffer_size_y = STREAM_H,
                    .dst_offset_x = copy_dst_x,
                    .dst_offset_y = copy_dst_y,
                    .copy_size_x = copy_width,
                    .copy_size_y = copy_height,
                    .pixel_format_unique_id = {
                        .color_type_id = COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB565),
                    },
                };

                /* Source-Cache nach Memory flush, damit DMA korrekte Daten
                 * liest (CPU hat in s_stream_buf geschrieben, evtl noch
                 * im L1/L2 Cache). */
                esp_cache_msync(s_stream_buf, s_stream_buf_size,
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

                /* Copy in fb0 */
                cfg.dst_buffer = s_dpi_fbs[0];
                esp_err_t ce = esp_async_fbcpy(s_fbcpy_handle, &cfg,
                                                stream_copy_done_cb, NULL);
                if (ce == ESP_OK) {
                    xSemaphoreTake(s_copy_done_sem, pdMS_TO_TICKS(100));
                } else {
                    ESP_LOGW(TAG, "fbcpy fb0: %s", esp_err_to_name(ce));
                }

                /* Copy in fb1 */
                cfg.dst_buffer = s_dpi_fbs[1];
                ce = esp_async_fbcpy(s_fbcpy_handle, &cfg,
                                      stream_copy_done_cb, NULL);
                if (ce == ESP_OK) {
                    xSemaphoreTake(s_copy_done_sem, pdMS_TO_TICKS(100));
                } else {
                    ESP_LOGW(TAG, "fbcpy fb1: %s", esp_err_to_name(ce));
                }

                /* Destination-Cache flushen ueber die tatsaechlich geschriebene
                 * Region - damit Display-DMA die frischen Daten sieht. Im
                 * fullscreen-Mode = ganzes FB (2 MB), sonst nur Stream-Region
                 * (1.6 MB). */
                size_t region_bytes      = (size_t)STREAM_W * copy_height * STREAM_BPP;
                size_t dst_offset_bytes  = copy_dst_y * STREAM_W * STREAM_BPP;
                esp_cache_msync((uint8_t *)s_dpi_fbs[0] + dst_offset_bytes, region_bytes,
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
                esp_cache_msync((uint8_t *)s_dpi_fbs[1] + dst_offset_bytes, region_bytes,
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

                bsp_display_unlock();
            } else {
                ESP_LOGW(TAG, "fb-write skipped: display_lock(100) timeout");
            }

            frame_count++;

            int64_t now_us = esp_timer_get_time();
            if (now_us - last_log_us > 1000000) {
                float secs = (now_us - start_us) / 1000000.0;
                float fps = frame_count / secs;
                ESP_LOGI(TAG, "fps=%.1f  dec=%dms  frames=%d  skipped=%d  jpeg=%d KB",
                    fps, last_dec_ms, frame_count, skipped_count, last_jpeg_size / 1024);
                last_log_us = now_us;
            }
        }
reconnect:
        ESP_LOGW(TAG, "Stream lost, reconnecting...");
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void stream_pipeline_start(lv_obj_t *parent)
{
    /* S5-04: parent unbenutzt (kein lv_canvas mehr). Stream geht direkt
     * in den DPI-FB. Signatur bleibt fuer Source-Compat mit main.c. */
    (void)parent;
    xTaskCreatePinnedToCore(mjpeg_task, "mjpeg", 16384, NULL, 5, NULL, 0);
}

/*
 * S4-03 .. S5-03 hatten hier stream_pipeline_attach_to_overlay und
 * detach_from_overlay fuer das lv_canvas-Reparenting waehrend des
 * Klingelns. Mit dem Direct-FB-Pfad (S5-04 Teil A) gibt es keinen
 * lv_canvas mehr. Mit der Klingel-im-Livestream-Architektur (S5-04
 * Teil C) gibt es auch keinen Reparent-Bedarf mehr - die Klingel-UI
 * liegt direkt ueber dem laufenden Stream.
 *
 * Die Funktionen sind in Teil C aus dem Header und der Quelle
 * entfernt. Reflog hat die alten Implementations falls jemals
 * gebraucht.
 */
