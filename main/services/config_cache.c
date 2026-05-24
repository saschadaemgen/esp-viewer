/*
 * config_cache.c - NVS-Cache fuer /esp/config JSON-Snapshot
 */

#include "config_cache.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "CFGCACHE";

#define NVS_NAMESPACE  "unifix"
#define NVS_KEY        "config_cache"

esp_err_t config_cache_save(const char *json_str)
{
    if (!json_str) {
        ESP_LOGE(TAG, "save: json_str is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strlen(json_str);
    if (len >= CONFIG_CACHE_MAX_LEN) {
        ESP_LOGE(TAG, "save: json too long (%d bytes, max %d)",
                 (int)len, CONFIG_CACHE_MAX_LEN - 1);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save: nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY, json_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save: nvs_set_str failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save: nvs_commit failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Config cache saved (%d bytes)", (int)len);
    return ESP_OK;
}

esp_err_t config_cache_load(char *out_buf, size_t buf_size)
{
    if (!out_buf || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "load: namespace '%s' not initialized", NVS_NAMESPACE);
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "load: nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t required = buf_size;
    err = nvs_get_str(handle, NVS_KEY, out_buf, &required);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "load: no cache entry yet");
        return ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "load: buffer too small (need %d bytes)", (int)required);
        return ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "load: nvs_get_str failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Config cache loaded (%d bytes)", (int)strlen(out_buf));
    return ESP_OK;
}

esp_err_t config_cache_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK; /* namespace doesn't exist = nothing to clear */
        }
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config cache cleared");
    }
    return err;
}

bool config_cache_has(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required = 0;
    err = nvs_get_str(handle, NVS_KEY, NULL, &required);
    nvs_close(handle);

    return (err == ESP_OK && required > 1);
}
