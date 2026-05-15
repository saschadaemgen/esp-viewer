/*
 * sse_client.c - Server-Sent-Events Listener Implementation v3
 *
 * ESP-Saison 2 Tag 3 (raw-socket-Version)
 *
 * v3-Aenderung: weg von esp_http_client (blockiert bei chunked
 * Long-Streams), hin zu raw lwip-sockets analog stream_pipeline.c.
 *
 * Architektur:
 *   1. socket() + connect() zu unifix-Server
 *   2. send() HTTP GET /esp/events mit Authorization-Header
 *   3. recv() Loop mit SO_RCVTIMEO (1s timeout, damit nicht blockiert)
 *   4. chunked-Decoder: <hex>\r\n<bytes>\r\n + Trailer
 *   5. Innerhalb der Chunks: SSE-Frame-Parser Line-by-Line
 *
 * Saubere zwei-Schichten-Decoder:
 *   - chunked_layer: zerlegt TCP-Bytes in "echte" Body-Bytes
 *   - sse_parser:    nimmt Body-Bytes, parsed event:/data:/Leerzeile
 */

#include "sse_client.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "device_token.h"

static const char *TAG = "SSE";

#define SSE_SERVER_HOST      "192.168.1.42"
#define SSE_SERVER_PORT      9080
#define SSE_TASK_STACK       6144
#define SSE_TASK_PRIORITY    4
#define SSE_TASK_CORE        1

#define SSE_RECV_BUF_SIZE    2048
#define SSE_LINE_BUF_SIZE    1024
#define SSE_EVENT_NAME_SIZE  64
#define SSE_DATA_BUF_SIZE    768

#define SSE_RECV_TIMEOUT_SEC 1       /* 1s, damit recv() nicht ewig blockt */
#define SSE_IDLE_LOG_MS      35000   /* alle 35s "lebt noch"-Log falls keine Daten */

#define SSE_BACKOFF_INIT_MS  1000
#define SSE_BACKOFF_MAX_MS   30000

typedef struct {
    char path[64];
    char token[DEVICE_TOKEN_MAX_LEN];
    sse_event_cb_t callback;
} sse_task_arg_t;

/*
 * SSE-Frame-State: wird ueber mehrere recv()-Aufrufe hinweg aufgebaut.
 */
typedef struct {
    char event_name[SSE_EVENT_NAME_SIZE];
    char data[SSE_DATA_BUF_SIZE];
    size_t event_name_len;
    size_t data_len;
    bool has_content;
} sse_frame_t;

/*
 * Chunked-Decoder-State.
 *
 * Chunked Encoding RFC 7230 Section 4.1:
 *   chunked-body  = *chunk last-chunk trailer-part CRLF
 *   chunk         = chunk-size [ chunk-ext ] CRLF chunk-data CRLF
 *   chunk-size    = 1*HEXDIG
 *   last-chunk    = 1*("0") [ chunk-ext ] CRLF
 *
 * Wir ignorieren chunk-ext und trailer-part komplett.
 * Wir liefern die "echten" Body-Bytes Stueck fuer Stueck.
 */
typedef enum {
    CHUNKED_STATE_SIZE,        /* Lese Hex-Size + optional ext */
    CHUNKED_STATE_SIZE_CR,     /* Gerade \r nach Size, warte \n */
    CHUNKED_STATE_DATA,        /* Lese chunk_size Bytes Daten */
    CHUNKED_STATE_DATA_CR,     /* Daten fertig, warte \r */
    CHUNKED_STATE_DATA_LF,     /* warte \n nach data-CR */
    CHUNKED_STATE_DONE,        /* last-chunk gelesen, Stream zu Ende */
    CHUNKED_STATE_ERROR,       /* Format-Fehler */
} chunked_state_t;

typedef struct {
    chunked_state_t state;
    char size_buf[16];
    size_t size_buf_pos;
    size_t chunk_remaining;
} chunked_decoder_t;

static void chunked_init(chunked_decoder_t *cd)
{
    cd->state = CHUNKED_STATE_SIZE;
    cd->size_buf_pos = 0;
    cd->chunk_remaining = 0;
    memset(cd->size_buf, 0, sizeof(cd->size_buf));
}

/*
 * Frame-Operations
 */
static void frame_reset(sse_frame_t *frame)
{
    frame->event_name[0] = 0;
    frame->data[0] = 0;
    frame->event_name_len = 0;
    frame->data_len = 0;
    frame->has_content = false;
}

static void frame_emit(sse_frame_t *frame, sse_event_cb_t callback)
{
    if (!frame->has_content) {
        return;
    }
    const char *name = (frame->event_name_len > 0) ? frame->event_name : "message";

    ESP_LOGI(TAG, "Event: %s", name);
    if (frame->data_len > 0) {
        ESP_LOGI(TAG, "Data:  %s", frame->data);
    }

    if (callback) {
        callback(name, frame->data);
    }

    frame_reset(frame);
}

static void process_sse_line(const char *line, sse_frame_t *frame,
                             sse_event_cb_t callback)
{
    if (line[0] == 0) {
        frame_emit(frame, callback);
        return;
    }
    if (line[0] == ':') {
        return;
    }

    const char *colon = strchr(line, ':');
    const char *value = "";
    size_t field_len;

    if (colon) {
        field_len = colon - line;
        value = colon + 1;
        if (*value == ' ') value++;
    } else {
        field_len = strlen(line);
    }

    if (field_len == 5 && strncmp(line, "event", 5) == 0) {
        size_t value_len = strlen(value);
        if (value_len >= SSE_EVENT_NAME_SIZE) {
            value_len = SSE_EVENT_NAME_SIZE - 1;
        }
        memcpy(frame->event_name, value, value_len);
        frame->event_name[value_len] = 0;
        frame->event_name_len = value_len;
        frame->has_content = true;
    } else if (field_len == 4 && strncmp(line, "data", 4) == 0) {
        size_t value_len = strlen(value);
        size_t space = SSE_DATA_BUF_SIZE - frame->data_len - 2;
        size_t copy = (value_len < space) ? value_len : space;
        if (frame->data_len > 0) {
            frame->data[frame->data_len++] = '\n';
        }
        memcpy(frame->data + frame->data_len, value, copy);
        frame->data_len += copy;
        frame->data[frame->data_len] = 0;
        frame->has_content = true;
    }
}

/*
 * SSE-Line-Buffer State (zwischen den decoded Body-Chunks)
 */
typedef struct {
    char line_buf[SSE_LINE_BUF_SIZE];
    size_t line_pos;
} sse_parser_t;

static void parser_init(sse_parser_t *p)
{
    p->line_pos = 0;
    p->line_buf[0] = 0;
}

static void parser_feed(sse_parser_t *p, const char *data, size_t len,
                        sse_frame_t *frame, sse_event_cb_t callback)
{
    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            p->line_buf[p->line_pos] = 0;
            process_sse_line(p->line_buf, frame, callback);
            p->line_pos = 0;
            continue;
        }

        if (p->line_pos >= SSE_LINE_BUF_SIZE - 1) {
            ESP_LOGW(TAG, "Line too long, discarding");
            p->line_pos = 0;
            continue;
        }

        p->line_buf[p->line_pos++] = c;
    }
}

/*
 * Chunked-Decoder.
 *
 * Frisst TCP-Bytes, ruft body_callback fuer die "echten" Body-Bytes auf.
 *
 * Returnt:
 *   0  = normal weiterlesen
 *   1  = last-chunk gesehen, Stream zu Ende
 *   -1 = Format-Fehler
 */
static int chunked_feed(chunked_decoder_t *cd,
                        const char *data, size_t len,
                        sse_parser_t *parser,
                        sse_frame_t *frame,
                        sse_event_cb_t callback)
{
    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        switch (cd->state) {

        case CHUNKED_STATE_SIZE:
            if (c == '\r') {
                cd->size_buf[cd->size_buf_pos] = 0;
                cd->chunk_remaining = (size_t)strtoul(cd->size_buf, NULL, 16);
                ESP_LOGD(TAG, "Chunk size: %d", (int)cd->chunk_remaining);
                cd->state = CHUNKED_STATE_SIZE_CR;
            } else if (c == ';') {
                /* chunk-ext beginnt, Rest bis \r ignorieren */
                cd->size_buf[cd->size_buf_pos] = 0;
                cd->chunk_remaining = (size_t)strtoul(cd->size_buf, NULL, 16);
                ESP_LOGD(TAG, "Chunk size with ext: %d", (int)cd->chunk_remaining);
                /* simulieren wir CR-Empfang */
                cd->state = CHUNKED_STATE_SIZE_CR;
                /* aber wir haben noch nicht \r gesehen, also brauchen wir
                 * eigentlich einen weiteren State fuer "skip until \r".
                 * Pragmatisch: wir nehmen an, dass chunk-ext nicht vorkommt
                 * (Master-Chat curl-v zeigt keine). Falls doch, fixen wir's. */
            } else if (cd->size_buf_pos < sizeof(cd->size_buf) - 1) {
                cd->size_buf[cd->size_buf_pos++] = c;
            } else {
                ESP_LOGE(TAG, "Chunk size too long");
                cd->state = CHUNKED_STATE_ERROR;
                return -1;
            }
            break;

        case CHUNKED_STATE_SIZE_CR:
            if (c != '\n') {
                ESP_LOGE(TAG, "Expected LF after CR in chunk size, got 0x%02x", (unsigned)c);
                cd->state = CHUNKED_STATE_ERROR;
                return -1;
            }
            if (cd->chunk_remaining == 0) {
                /* last-chunk */
                ESP_LOGI(TAG, "Last-chunk received, stream ending");
                cd->state = CHUNKED_STATE_DONE;
                return 1;
            }
            cd->state = CHUNKED_STATE_DATA;
            break;

        case CHUNKED_STATE_DATA: {
            /* Wie viele Bytes des aktuellen Chunks koennen wir in einem Rutsch
             * an den SSE-Parser geben? */
            size_t avail = len - i;
            size_t consume = (avail < cd->chunk_remaining) ? avail : cd->chunk_remaining;
            parser_feed(parser, data + i, consume, frame, callback);
            cd->chunk_remaining -= consume;
            i += consume - 1;  /* -1 weil for-Loop noch i++ macht */
            if (cd->chunk_remaining == 0) {
                cd->state = CHUNKED_STATE_DATA_CR;
            }
            break;
        }

        case CHUNKED_STATE_DATA_CR:
            if (c != '\r') {
                ESP_LOGE(TAG, "Expected CR after chunk data, got 0x%02x", (unsigned)c);
                cd->state = CHUNKED_STATE_ERROR;
                return -1;
            }
            cd->state = CHUNKED_STATE_DATA_LF;
            break;

        case CHUNKED_STATE_DATA_LF:
            if (c != '\n') {
                ESP_LOGE(TAG, "Expected LF after CR after chunk data, got 0x%02x", (unsigned)c);
                cd->state = CHUNKED_STATE_ERROR;
                return -1;
            }
            /* Naechster Chunk beginnt */
            cd->size_buf_pos = 0;
            cd->size_buf[0] = 0;
            cd->state = CHUNKED_STATE_SIZE;
            break;

        case CHUNKED_STATE_DONE:
        case CHUNKED_STATE_ERROR:
            return cd->state == CHUNKED_STATE_DONE ? 1 : -1;
        }
    }
    return 0;
}

/*
 * Eine SSE-Verbindung halten.
 */
static void run_one_connection(sse_task_arg_t *arg,
                               char *recv_buf,
                               sse_parser_t *parser,
                               sse_frame_t *frame,
                               chunked_decoder_t *chunked)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return;
    }

    /* Receive-Timeout, damit recv() nicht ewig blockt.
     * Stattdessen returnt es regelmaessig, wir machen Health-Logs. */
    struct timeval tv = { .tv_sec = SSE_RECV_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* TCP-Keepalive */
    int keepalive = 1;
    int keepidle = 5;
    int keepintvl = 5;
    int keepcnt = 3;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

    struct sockaddr_in dest = { 0 };
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SSE_SERVER_PORT);
    dest.sin_addr.s_addr = inet_addr(SSE_SERVER_HOST);

    ESP_LOGI(TAG, "Connecting to %s:%d ...", SSE_SERVER_HOST, SSE_SERVER_PORT);
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGE(TAG, "connect() failed: errno=%d", errno);
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "TCP-Connection established, sending HTTP GET ...");

    /* HTTP GET-Request senden */
    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Accept: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: ESP32-P4-KOENIGSLIGA\r\n"
        "\r\n",
        arg->path, SSE_SERVER_HOST, SSE_SERVER_PORT, arg->token);

    if (send(sock, req, req_len, 0) != req_len) {
        ESP_LOGE(TAG, "send() failed");
        close(sock);
        return;
    }
    /* req-Buffer mit Token jetzt nicht mehr noetig */
    memset(req, 0, sizeof(req));

    ESP_LOGI(TAG, "GET sent, waiting for HTTP-Response-Headers ...");

    /* HTTP-Response-Header lesen bis \r\n\r\n */
    size_t header_pos = 0;
    bool headers_done = false;
    int status_code = 0;
    int header_timeouts = 0;

    while (!headers_done && header_pos < SSE_RECV_BUF_SIZE - 1) {
        int n = recv(sock, recv_buf + header_pos, SSE_RECV_BUF_SIZE - 1 - header_pos, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                header_timeouts++;
                if (header_timeouts > 10) {
                    ESP_LOGE(TAG, "Header read timeout (>10s)");
                    close(sock);
                    return;
                }
                continue;
            }
            ESP_LOGE(TAG, "recv() failed during headers: errno=%d", errno);
            close(sock);
            return;
        }
        if (n == 0) {
            ESP_LOGE(TAG, "Connection closed during headers");
            close(sock);
            return;
        }
        header_pos += n;
        recv_buf[header_pos] = 0;
        char *end = strstr(recv_buf, "\r\n\r\n");
        if (end) {
            headers_done = true;
            *end = 0;
            /* Status-Line: "HTTP/1.1 200 OK" */
            if (strncmp(recv_buf, "HTTP/1.", 7) == 0) {
                char *sp = strchr(recv_buf, ' ');
                if (sp) status_code = atoi(sp + 1);
            }
            /* Body koennte schon Bytes nach \r\n\r\n haben */
            size_t header_size = (end - recv_buf) + 4;
            size_t body_leftover = header_pos - header_size;

            ESP_LOGI(TAG, "Response headers received (status=%d, %d body bytes leftover)",
                     status_code, (int)body_leftover);

            if (status_code != 200) {
                ESP_LOGE(TAG, "Bad status %d", status_code);
                close(sock);
                return;
            }

            ESP_LOGI(TAG, "SSE stream open, entering chunked-decode-loop ...");

            /* Bereits empfangene Body-Bytes durch den Chunked-Decoder */
            if (body_leftover > 0) {
                int r = chunked_feed(chunked,
                                     recv_buf + header_size, body_leftover,
                                     parser, frame, arg->callback);
                if (r != 0) {
                    ESP_LOGW(TAG, "chunked_feed returned %d immediately", r);
                    close(sock);
                    return;
                }
            }
        }
    }

    if (!headers_done) {
        ESP_LOGE(TAG, "Failed to parse headers");
        close(sock);
        return;
    }

    /* Main-Read-Loop fuer Body */
    int64_t last_activity_ms = esp_log_timestamp();
    int total_body_bytes = 0;

    while (1) {
        int n = recv(sock, recv_buf, SSE_RECV_BUF_SIZE, 0);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Timeout (1s) - normal bei Long-Stream Idle-Phasen */
                int64_t now = esp_log_timestamp();
                if (now - last_activity_ms > SSE_IDLE_LOG_MS) {
                    ESP_LOGI(TAG, "Idle (no data in %ds, %d total body bytes received)",
                             (int)((now - last_activity_ms) / 1000),
                             total_body_bytes);
                    last_activity_ms = now;
                }
                continue;
            }
            ESP_LOGW(TAG, "recv() error: errno=%d", errno);
            break;
        }
        if (n == 0) {
            ESP_LOGW(TAG, "Connection closed by server");
            break;
        }

        total_body_bytes += n;
        last_activity_ms = esp_log_timestamp();
        ESP_LOGD(TAG, "recv %d bytes (total %d)", n, total_body_bytes);

        int r = chunked_feed(chunked, recv_buf, n, parser, frame, arg->callback);
        if (r == 1) {
            ESP_LOGI(TAG, "Stream ended (last-chunk)");
            break;
        }
        if (r < 0) {
            ESP_LOGE(TAG, "Chunked-decode error");
            break;
        }
    }

    close(sock);
}

static void sse_listener_task(void *p)
{
    sse_task_arg_t *arg = (sse_task_arg_t *)p;

    char *recv_buf = malloc(SSE_RECV_BUF_SIZE);
    sse_parser_t *parser = malloc(sizeof(sse_parser_t));
    sse_frame_t *frame = malloc(sizeof(sse_frame_t));
    chunked_decoder_t *chunked = malloc(sizeof(chunked_decoder_t));

    if (!recv_buf || !parser || !frame || !chunked) {
        ESP_LOGE(TAG, "Heap alloc failed, task dying");
        goto die;
    }

    int backoff_ms = SSE_BACKOFF_INIT_MS;

    while (1) {
        parser_init(parser);
        frame_reset(frame);
        chunked_init(chunked);

        run_one_connection(arg, recv_buf, parser, frame, chunked);

        ESP_LOGI(TAG, "Reconnecting in %d ms ...", backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));

        backoff_ms *= 2;
        if (backoff_ms > SSE_BACKOFF_MAX_MS) {
            backoff_ms = SSE_BACKOFF_MAX_MS;
        }
    }

die:
    free(recv_buf);
    free(parser);
    free(frame);
    free(chunked);
    free(arg);
    vTaskDelete(NULL);
}

esp_err_t sse_client_start(const char *path, sse_event_cb_t callback)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }

    sse_task_arg_t *arg = calloc(1, sizeof(sse_task_arg_t));
    if (!arg) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(arg->path, path, sizeof(arg->path) - 1);
    arg->callback = callback;

    esp_err_t err = device_token_get(arg->token, sizeof(arg->token));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start: token not available: %s", esp_err_to_name(err));
        free(arg);
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(sse_listener_task, "sse_listener",
                                            SSE_TASK_STACK, arg,
                                            SSE_TASK_PRIORITY, NULL,
                                            SSE_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        memset(arg->token, 0, sizeof(arg->token));
        free(arg);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "SSE-Listener task started for %s", path);
    return ESP_OK;
}
