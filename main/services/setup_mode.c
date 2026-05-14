/*
 * setup_mode.c - Setup-Modus Implementation v2
 *
 * ESP-Saison 2 Tag 2
 *
 * Liest den Token von UART0 (Standard-Console).
 *
 * v2: Installiert UART-Driver explizit (war v1-Bug).
 *
 * Format:
 *   - Knutsche-Hase pastet den Token in `idf.py monitor`
 *   - Druckt Enter (newline)
 *   - Wir lesen Zeichen-fuer-Zeichen bis Newline
 *   - Erlaubte Zeichen: A-Z a-z 0-9 - _  (Base64URL)
 *   - Sonstige Zeichen werden ignoriert (z.B. \r vor \n)
 *
 * Saison-2-Praxis: dieser Pfad wird selten genutzt weil
 * das Provisioning normalerweise via tools/provision-esp.py
 * (NVS-Partition-Flash via esptool) passiert. Setup-Mode bleibt
 * als Field-Service-Fallback.
 */

#include "setup_mode.h"

#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "device_token.h"

static const char *TAG = "SETUP";

#define SETUP_UART_NUM       UART_NUM_0
#define SETUP_TOKEN_BUF_LEN  128
#define SETUP_UART_BUF_LEN   256

static bool is_token_char(char c)
{
    /* Base64URL: A-Z a-z 0-9 - _ */
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c == '-' || c == '_') return true;
    return false;
}

static esp_err_t setup_uart_install(void)
{
    /*
     * UART0 ist im Standard-ESP-IDF schon fuer den Console-Output
     * konfiguriert (esp_log, printf etc), aber der Driver wurde
     * via Console-Init initialisiert was uart_read_bytes nicht
     * unterstuetzt. Wir muessen den Driver explizit installieren.
     *
     * Falls bereits installiert: ESP_ERR_INVALID_STATE, ignorieren.
     */
    esp_err_t err = uart_driver_install(SETUP_UART_NUM,
                                        SETUP_UART_BUF_LEN, 0, 0, NULL, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        /* schon installiert, ok */
        return ESP_OK;
    }
    return err;
}

static void setup_uart_uninstall(void)
{
    uart_driver_delete(SETUP_UART_NUM);
}

void setup_mode_run(void)
{
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  SETUP MODE - No device_token in NVS");
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  Paste your Bearer-Token below and press Enter.");
    ESP_LOGI(TAG, "  (Token is Base64URL, e.g. 43 chars)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  Token will be saved to NVS, then ESP restarts.");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  Production-Workflow tip: use tools/provision-esp.py");
    ESP_LOGI(TAG, "  instead of console paste for bulk provisioning.");
    ESP_LOGI(TAG, "================================================");

    esp_err_t err = setup_uart_install();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Halting in setup mode without input.");
        ESP_LOGE(TAG, "Use tools/provision-esp.py to flash NVS, then restart.");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000));
            ESP_LOGI(TAG, "Setup mode: waiting for NVS provisioning ...");
        }
        return;
    }

    char buf[SETUP_TOKEN_BUF_LEN] = {0};
    size_t pos = 0;
    int idle_count = 0;

    while (1) {
        uint8_t c;
        int n = uart_read_bytes(SETUP_UART_NUM, &c, 1, pdMS_TO_TICKS(1000));
        if (n <= 0) {
            /* Timeout: heartbeat-Log alle 10 Sek damit Knutsche-Hase sieht,
             * dass der ESP wartet. */
            if (++idle_count >= 10) {
                ESP_LOGI(TAG, "Waiting for token paste + Enter ...");
                idle_count = 0;
            }
            continue;
        }
        idle_count = 0;

        if (c == '\n' || c == '\r') {
            if (pos == 0) {
                /* Leer-Zeile, ignorieren */
                continue;
            }
            /* Newline = Ende der Eingabe */
            buf[pos] = 0;
            break;
        }

        if (!is_token_char((char)c)) {
            /* Stilles Ignorieren von Tabs, Spaces, ANSI-Codes etc. */
            continue;
        }

        if (pos >= sizeof(buf) - 1) {
            ESP_LOGE(TAG, "Token too long (max %d chars)", (int)sizeof(buf) - 1);
            ESP_LOGE(TAG, "Restarting setup mode ...");
            pos = 0;
            memset(buf, 0, sizeof(buf));
            continue;
        }

        buf[pos++] = (char)c;
    }

    ESP_LOGI(TAG, "Received %d chars, saving to NVS ...", (int)pos);

    err = device_token_set(buf);
    /* Buffer mit Token sofort ueberschreiben, nicht im RAM rumliegen lassen */
    memset(buf, 0, sizeof(buf));

    setup_uart_uninstall();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save token: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Restarting in 5 seconds, try again ...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  Token saved successfully!");
    ESP_LOGI(TAG, "  Restarting in 2 seconds ...");
    ESP_LOGI(TAG, "================================================");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}
