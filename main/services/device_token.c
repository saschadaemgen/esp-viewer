/*
 * device_token.c - Bearer-Token-Storage in NVS Implementation
 *
 * ESP-Saison 2 Tag 2
 */

#include "device_token.h"

#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "TOKEN";

#define NVS_NAMESPACE "unifix"
#define NVS_KEY_TOKEN "device_token"

esp_err_t device_token_get(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "Namespace '%s' not found", NVS_NAMESPACE);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t required = 0;
    err = nvs_get_str(handle, NVS_KEY_TOKEN, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str sizeof failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    if (required > buf_size) {
        ESP_LOGE(TAG, "Buffer too small: need %d, have %d", (int)required, (int)buf_size);
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    err = nvs_get_str(handle, NVS_KEY_TOKEN, buf, &required);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str read failed: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Token NIE in Logs ausgeben! Nur Laenge fuer Diagnose.
     */
    ESP_LOGI(TAG, "Token loaded from NVS (length %d)", (int)required - 1);
    return ESP_OK;
}

esp_err_t device_token_set(const char *token)
{
    if (!token || token[0] == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strlen(token);
    if (len >= DEVICE_TOKEN_MAX_LEN) {
        ESP_LOGE(TAG, "Token too long: %d (max %d)", (int)len, DEVICE_TOKEN_MAX_LEN - 1);
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open RW failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_TOKEN, token);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Token NIE in Logs ausgeben! Nur Laenge.
     */
    ESP_LOGI(TAG, "Token saved to NVS (length %d)", (int)len);
    return ESP_OK;
}

esp_err_t device_token_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Namespace gibt's nicht, also auch kein Token. Erfolg. */
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY_TOKEN);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Key nicht da, auch okay. */
        err = ESP_OK;
    } else if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Token cleared from NVS");
    return err;
}

bool device_token_has(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required = 0;
    err = nvs_get_str(handle, NVS_KEY_TOKEN, NULL, &required);
    nvs_close(handle);

    return (err == ESP_OK && required > 1);
}
