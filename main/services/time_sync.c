/*
 * time_sync.c - NTP-Sync + Locale-Helper Implementation
 */

#include "time_sync.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TIMESYNC";

/* Europe/Berlin POSIX-TZ-String.
 * CET = Central European Time = UTC+1
 * CEST = Central European Summer Time = UTC+2
 * Switch:
 *   M3.5.0    -> last Sunday of March
 *   M10.5.0/3 -> last Sunday of October, 03:00 local
 */
#define TZ_EUROPE_BERLIN  "CET-1CEST,M3.5.0,M10.5.0/3"

#define NTP_SERVER        "pool.ntp.org"

/* Sync-Status. Wird vom SNTP-Sync-Callback gesetzt. */
static volatile bool s_synced = false;
static bool s_started = false;

/* ---------------- Locale arrays ---------------- */

/* tm_wday: 0=Sunday .. 6=Saturday */
static const char *wochentage_de[] = {
    "Sonntag", "Montag", "Dienstag", "Mittwoch",
    "Donnerstag", "Freitag", "Samstag"
};

/* tm_mon: 0=January .. 11=December */
static const char *monate_de[] = {
    "Januar", "Februar", "M\xc3\xa4rz", "April", "Mai", "Juni",
    "Juli", "August", "September", "Oktober", "November", "Dezember"
};

static const char *wochentage_en[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

static const char *monate_en[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/* Kurze All-Caps-Formen fuer die Topbar */
static const char *wochentage_de_short[] = {
    "SO", "MO", "DI", "MI", "DO", "FR", "SA"
};
static const char *monate_de_short[] = {
    "JAN", "FEB", "MAR", "APR", "MAI", "JUN",
    "JUL", "AUG", "SEP", "OKT", "NOV", "DEZ"
};
static const char *wochentage_en_short[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};
static const char *monate_en_short[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

/* ---------------- Language helper ---------------- */

language_t time_sync_lang_from_str(const char *s)
{
    if (!s) return LANG_DE;
    if (strcmp(s, "en") == 0) return LANG_EN;
    return LANG_DE;
}

/* ---------------- SNTP callback ---------------- */

static void sntp_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "NTP time synchronized");
}

/* ---------------- Lifecycle ---------------- */

esp_err_t time_sync_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;

    ESP_LOGI(TAG, "Starting SNTP client (server=%s, tz=%s)",
             NTP_SERVER, TZ_EUROPE_BERLIN);

    /* Timezone setzen BEVOR SNTP startet, damit localtime() gleich
     * korrekt funktioniert sobald der erste Sync drin ist. */
    setenv("TZ", TZ_EUROPE_BERLIN, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    esp_sntp_init();

    return ESP_OK;
}

bool time_sync_wait_for_sync(uint32_t timeout_ms)
{
    const uint32_t poll_step_ms = 100;
    uint32_t elapsed = 0;
    while (!s_synced && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(poll_step_ms));
        elapsed += poll_step_ms;
    }
    return s_synced;
}

bool time_sync_is_synced(void)
{
    return s_synced;
}

/* ---------------- Format helpers ---------------- */

void time_sync_format_time(char *buf, size_t buflen, language_t lang)
{
    (void)lang;  /* reserviert fuer spaetere AM/PM-Option */

    if (!buf || buflen == 0) return;

    if (!s_synced) {
        snprintf(buf, buflen, "--:--");
        return;
    }

    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    snprintf(buf, buflen, "%02d:%02d", tm_local.tm_hour, tm_local.tm_min);
}

void time_sync_format_time_long(char *buf, size_t buflen, language_t lang)
{
    (void)lang;  /* time format is locale-neutral */

    if (!buf || buflen == 0) return;

    if (!s_synced) {
        snprintf(buf, buflen, "--:--:--");
        return;
    }

    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    snprintf(buf, buflen, "%02d:%02d:%02d",
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
}

void time_sync_format_date(char *buf, size_t buflen, language_t lang)
{
    if (!buf || buflen == 0) return;

    if (!s_synced) {
        buf[0] = 0;
        return;
    }

    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    int wday = tm_local.tm_wday;   /* 0..6 */
    int mon  = tm_local.tm_mon;    /* 0..11 */
    int day  = tm_local.tm_mday;   /* 1..31 */
    int year = tm_local.tm_year + 1900;

    /* Defensive bounds (corrupt tm_wday/tm_mon shouldn't crash us) */
    if (wday < 0 || wday > 6) wday = 0;
    if (mon  < 0 || mon  > 11) mon = 0;

    switch (lang) {
    case LANG_EN:
        /* "Sunday, May 17, 2026" */
        snprintf(buf, buflen, "%s, %s %d, %d",
                 wochentage_en[wday], monate_en[mon], day, year);
        break;
    case LANG_DE:
    default:
        /* "Sonntag, 17. Mai 2026" */
        snprintf(buf, buflen, "%s, %d. %s %d",
                 wochentage_de[wday], day, monate_de[mon], year);
        break;
    }
}

void time_sync_format_date_short(char *buf, size_t buflen, language_t lang)
{
    if (!buf || buflen == 0) return;

    if (!s_synced) {
        buf[0] = 0;
        return;
    }

    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    int wday = tm_local.tm_wday;
    int mon  = tm_local.tm_mon;
    int day  = tm_local.tm_mday;

    if (wday < 0 || wday > 6) wday = 0;
    if (mon  < 0 || mon  > 11) mon = 0;

    switch (lang) {
    case LANG_EN:
        /* "MON, MAY 18" (US-Stil: Monat zuerst) */
        snprintf(buf, buflen, "%s, %s %d",
                 wochentage_en_short[wday], monate_en_short[mon], day);
        break;
    case LANG_DE:
    default:
        /* "MO, 18. MAI" */
        snprintf(buf, buflen, "%s, %d. %s",
                 wochentage_de_short[wday], day, monate_de_short[mon]);
        break;
    }
}
