/*
 * wifi_config.c - WLAN-SSID + Passwort-Storage in NVS Implementation
 *
 * ESP-Saison 2
 *
 * Folgt dem device_token.c Pattern (gleicher Namespace "unifix",
 * gleicher Logging-Stil, gleiche Error-Handling-Konventionen).
 *
 * WICHTIG: Passwort NIE in Logs ausgeben. Nur Laengen.
 */

#include "wifi_config.h"

#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "WIFICFG";

#define NVS_NAMESPACE   "unifix"
#define NVS_KEY_SSID    "wifi_ssid"
#define NVS_KEY_PWD     "wifi_pwd"


/* ============================================================
 * Internes Helper: liest einen String aus NVS in buf.
 * Wird von get_ssid und get_password verwendet.
 * ============================================================ */
static esp_err_t nvs_read_string(const char *key, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t required = 0;
    err = nvs_get_str(handle, key, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str(%s) sizeof failed: %s", key, esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    if (required > buf_size) {
        ESP_LOGE(TAG, "Buffer too small for %s: need %d, have %d",
                 key, (int)required, (int)buf_size);
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    err = nvs_get_str(handle, key, buf, &required);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str(%s) read failed: %s", key, esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}


esp_err_t wifi_config_get_ssid(char *buf, size_t buf_size)
{
    esp_err_t err = nvs_read_string(NVS_KEY_SSID, buf, buf_size);
    if (err == ESP_OK) {
        /* SSID darf geloggt werden, ist nicht sensitiv */
        ESP_LOGI(TAG, "SSID loaded from NVS: %s", buf);
    }
    return err;
}


esp_err_t wifi_config_get_password(char *buf, size_t buf_size)
{
    esp_err_t err = nvs_read_string(NVS_KEY_PWD, buf, buf_size);
    if (err == ESP_OK) {
        /* Passwort NIE im Log! Nur Laenge fuer Diagnose */
        ESP_LOGI(TAG, "Password loaded from NVS (length %d)", (int)strlen(buf));
    }
    return err;
}


esp_err_t wifi_config_set(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!password) {
        password = "";   /* Offene Netze erlauben */
    }

    size_t ssid_len = strlen(ssid);
    size_t pwd_len = strlen(password);

    if (ssid_len >= WIFI_SSID_MAX_LEN) {
        ESP_LOGE(TAG, "SSID too long: %d (max %d)",
                 (int)ssid_len, WIFI_SSID_MAX_LEN - 1);
        return ESP_ERR_INVALID_SIZE;
    }
    if (pwd_len >= WIFI_PWD_MAX_LEN) {
        ESP_LOGE(TAG, "Password too long: %d (max %d)",
                 (int)pwd_len, WIFI_PWD_MAX_LEN - 1);
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open RW failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(ssid) failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_PWD, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(password) failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WLAN config saved (SSID='%s', password length %d)",
             ssid, (int)pwd_len);
    return ESP_OK;
}


esp_err_t wifi_config_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    /* Beide Keys loeschen, NOT_FOUND ist okay */
    err = nvs_erase_key(handle, NVS_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY_PWD);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WLAN config cleared from NVS");
    return err;
}


bool wifi_config_has(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    /* Beide Keys muessen existieren und SSID darf nicht leer sein */
    size_t ssid_size = 0;
    err = nvs_get_str(handle, NVS_KEY_SSID, NULL, &ssid_size);
    if (err != ESP_OK || ssid_size <= 1) {
        nvs_close(handle);
        return false;
    }

    size_t pwd_size = 0;
    err = nvs_get_str(handle, NVS_KEY_PWD, NULL, &pwd_size);
    nvs_close(handle);

    /* pwd_size = 1 bedeutet nur NUL-Terminator, das waere offenes Netz - okay */
    return (err == ESP_OK);
}
