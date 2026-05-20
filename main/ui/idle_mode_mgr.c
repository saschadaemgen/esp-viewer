/*
 * idle_mode_mgr.c - Idle-Mode + Backlight Manager
 *
 * Drei Pfade je nach idle_view_mode:
 *
 *   SCREENSAVER + LIVESTREAM:
 *     Backlight bleibt IMMER brightness_idle.
 *     Inaktivitaets-Timeout (auto_screensaver_sec) loest Mode-Switch
 *     zurueck zum konfigurierten Default-View aus.
 *
 *   SCREEN_OFF:
 *     Backlight default brightness_idle.
 *     Nach screen_off_after_sec -> Backlight 0 (Display aus).
 *     Touch weckt auf via fade-in auf brightness_idle.
 *
 * Polling: 500ms LVGL-Timer (laeuft im LVGL-Task-Context, also kein
 * extra display_lock noetig fuer Mode-Switch-Aufrufe).
 *
 * Backlight-Fade:
 *   Wir benutzen lv_anim auf eine globale Variable mit
 *   apply_backlight als Step-Callback. Animation laeuft auch im
 *   LVGL-Task. Klingel-Wake ist hart (kein Fade).
 */

#include "idle_mode_mgr.h"
#include "scr_idle.h"
#include "scr_ringing.h"
#include "services/unifix_config.h"

#include <stdbool.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lvgl.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

static const char *TAG = "IDLEMGR";

#define POLL_INTERVAL_MS         500
#define FADE_DURATION_MS         300
#define WAKE_HARD_BRIGHTNESS     100

/* ---------------- Module state ---------------- */

static SemaphoreHandle_t s_mutex = NULL;
static lv_timer_t *s_poll_timer = NULL;
static bool s_started = false;

/* Konfiguration (mutex-protected) */
static idle_view_mode_t s_mode = IDLE_MODE_SCREENSAVER;
static int  s_auto_screensaver_sec = UNIFIX_CONFIG_DEFAULT_AUTO_SCREENSAVER_SEC;
static int  s_screen_off_after_sec = UNIFIX_CONFIG_DEFAULT_SCREEN_OFF_SEC;
static int  s_brightness_idle = UNIFIX_CONFIG_DEFAULT_BRIGHTNESS_IDLE;

/* Runtime-State */
static backlight_reason_t s_reason = BACKLIGHT_REASON_IDLE;
static bool s_screen_is_off = false;
static int64_t s_last_activity_us = 0;

/* S4-01: Vor doorbell_start gemerkter Bildschirmschoner-State.
 * Beim doorbell_end wieder hergestellt, damit der User nach dem
 * Auflegen wieder im Modus landet in dem er war. */
static bool s_doorbell_saved_was_screensaver = false;

/* ---------------- Helpers ---------------- */

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

/*
 * Map server-side 0..100 percent value to the BSP's accepted
 * 0..BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX range.
 */
static int scale_to_bsp(int server_percent)
{
    if (server_percent <= 0) return 0;
    if (server_percent >= 100) return BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX;
    return (server_percent * BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX + 50) / 100;
}

static int s_current_brightness = 0;

static void apply_backlight(int server_percent)
{
    if (server_percent < 0) server_percent = 0;
    if (server_percent > 100) server_percent = 100;

    int bsp_value = scale_to_bsp(server_percent);
    esp_err_t err = bsp_display_brightness_set(bsp_value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_display_brightness_set(%d) failed: %s",
                 bsp_value, esp_err_to_name(err));
    }
    s_current_brightness = server_percent;
}

/* ---------------- Fade-Animation ---------------- */

static void fade_anim_exec(void *var, int32_t v)
{
    (void)var;
    apply_backlight((int)v);
}

/*
 * Start a smooth fade from current brightness to target.
 * Called from LVGL-task-context (poll timer / notify_activity).
 * Hard-set if delta is 0.
 */
static void start_fade(int from, int to)
{
    if (from == to) {
        apply_backlight(to);
        return;
    }

    /* Cancel any previous fade so they don't fight. */
    lv_anim_delete(NULL, fade_anim_exec);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, NULL);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, FADE_DURATION_MS);
    lv_anim_set_exec_cb(&a, fade_anim_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ---------------- Sleep / Wake helpers (LVGL-task-context) ---------------- */

static void sleep_screen_internal(void)
{
    if (s_screen_is_off) return;
    s_screen_is_off = true;
    s_reason = BACKLIGHT_REASON_OFF;
    ESP_LOGI(TAG, "Sleep screen (SCREEN_OFF after timeout)");
    start_fade(s_current_brightness, 0);
}

static void wake_screen_internal(int target)
{
    if (!s_screen_is_off) {
        /* Already on - just ensure brightness matches target. */
        if (s_current_brightness != target) {
            start_fade(s_current_brightness, target);
        }
        return;
    }
    s_screen_is_off = false;
    s_reason = BACKLIGHT_REASON_IDLE;
    ESP_LOGI(TAG, "Wake screen -> %d%%", target);
    start_fade(0, target);
}

/* ---------------- Public API ---------------- */

void idle_mode_mgr_update_config(idle_view_mode_t idle_mode,
                                  int auto_screensaver_sec,
                                  int screen_off_after_sec,
                                  int brightness_idle)
{
    if (brightness_idle < 0) brightness_idle = 0;
    if (brightness_idle > 100) brightness_idle = 100;
    if (auto_screensaver_sec < 0) auto_screensaver_sec = 0;
    if (screen_off_after_sec < 0) screen_off_after_sec = 0;

    idle_view_mode_t old_mode;
    int old_brightness;
    bool screen_is_off;
    backlight_reason_t reason;

    if (!s_mutex) {
        /* update_config might be called before start(); just store. */
        s_mode = idle_mode;
        s_auto_screensaver_sec = auto_screensaver_sec;
        s_screen_off_after_sec = screen_off_after_sec;
        s_brightness_idle = brightness_idle;
        return;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    old_mode = s_mode;
    old_brightness = s_brightness_idle;
    screen_is_off = s_screen_is_off;
    reason = s_reason;

    s_mode = idle_mode;
    s_auto_screensaver_sec = auto_screensaver_sec;
    s_screen_off_after_sec = screen_off_after_sec;
    s_brightness_idle = brightness_idle;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Config: mode=%s auto_ss=%ds screen_off=%ds brightness=%d",
             unifix_config_mode_to_str(idle_mode),
             auto_screensaver_sec, screen_off_after_sec, brightness_idle);

    /* Live-Adjustments waehrend Klingel laeuft: nicht antasten,
     * doorbell_end resettet das eh wieder. */
    if (reason == BACKLIGHT_REASON_DOORBELL) {
        return;
    }

    /* Mode-Wechsel weg von SCREEN_OFF: falls Display gerade aus war,
     * weck es auf. */
    if (screen_is_off && idle_mode != IDLE_MODE_SCREEN_OFF) {
        if (bsp_display_lock(50)) {
            wake_screen_internal(brightness_idle);
            bsp_display_unlock();
        }
        return;
    }

    /* Brightness-Aenderung wenn Display an: sanftes Fade. */
    if (!screen_is_off && old_brightness != brightness_idle) {
        if (bsp_display_lock(50)) {
            start_fade(s_current_brightness, brightness_idle);
            bsp_display_unlock();
        }
    }

    (void)old_mode;
}

void idle_mode_mgr_notify_activity(void)
{
    int target;
    bool need_wake;

    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_last_activity_us = now_us();
    target = s_brightness_idle;
    need_wake = s_screen_is_off;
    xSemaphoreGive(s_mutex);

    if (need_wake) {
        if (bsp_display_lock(50)) {
            wake_screen_internal(target);
            bsp_display_unlock();
        }
    }

    /* LVGL-Inaktivitaets-Counter ebenfalls resetten. */
    if (bsp_display_lock(50)) {
        lv_display_trigger_activity(NULL);
        bsp_display_unlock();
    }
}

void idle_mode_mgr_doorbell_start(void)
{
    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_reason = BACKLIGHT_REASON_DOORBELL;
    s_screen_is_off = false;
    s_last_activity_us = now_us();
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Doorbell start -> backlight 100%% (hard)");

    /* Hart 100% ohne Fade. apply_backlight ist BSP-direct,
     * darf von jedem Task aufgerufen werden. Bevorzugt brechen wir
     * eine laufende Fade-Animation ab, damit sie nicht ueber unser
     * Hard-Set drueber malt.
     *
     * S4-01: visuelle Orchestrierung mit Mode-Snapshot.
     *   1. Bildschirmschoner-State merken (wir wollen ihn nach dem
     *      Auflegen restoren).
     *   2. Settings auto-schliessen falls offen.
     *   3. Stream-Mode forcieren damit der User die Kamera durch den
     *      35%-Scrim sieht (nicht Screensaver mit Uhr/Wetter).
     *   4. scr_ringing_show macht den 400ms-Fade-In.
     *
     * Settings auto-schliessen wird NICHT restored - der User wurde
     * mit Klingel unterbrochen, Settings re-oeffnen ist nur ein Tap.
     *
     * S4-05: Timeout 50ms war zu knapp. Im Stream-Modus ist der LVGL-
     * Refresh-Task durch den 9-FPS-Canvas-Invalidate gut beschaeftigt,
     * dazu nimmt der mjpeg_task selbst regelmaessig den Display-Lock fuer
     * lv_obj_invalidate. Der SSE/Worker-Task hatte unter dieser Last in
     * 50ms keine Chance auf den Lock -> ganzer Block (inkl. saved-view-
     * mode-Log UND scr_ringing_show) wurde uebersprungen. Symptom: Klingel
     * aus Stream-Modus zeigte NIE das Overlay. Im Bildschirmschoner ist
     * der Canvas hidden, kaum Render-Druck, 50ms reichten dort.
     *
     * 2000ms ist generous: bei normalem Betrieb dauert ein Frame-Render
     * <50ms, der Lock ist also fast immer schnell zu kriegen. Bei
     * Pathologie loggen wir's und fallen trotzdem nicht ganz aus. */
    if (bsp_display_lock(2000)) {
        lv_anim_delete(NULL, fade_anim_exec);
        lv_display_trigger_activity(NULL);

        s_doorbell_saved_was_screensaver = scr_idle_is_screensaver_mode();
        ESP_LOGI(TAG, "Doorbell start -> saved view-mode: %s",
                 s_doorbell_saved_was_screensaver ? "SCREENSAVER" : "STREAM");

        if (scr_idle_is_settings_shown()) {
            scr_idle_show_stream();  /* instant hide */
        }
        scr_idle_show_stream_mode();  /* idempotent */
        scr_ringing_show();

        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Doorbell start: display_lock(2000ms) TIMEOUT - overlay not shown!");
    }
    apply_backlight(WAKE_HARD_BRIGHTNESS);
}

void idle_mode_mgr_doorbell_end(void)
{
    int target;

    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_reason = BACKLIGHT_REASON_IDLE;
    s_last_activity_us = now_us();
    target = s_brightness_idle;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Doorbell end -> backlight %d%% (fade), restore=%s",
             target,
             s_doorbell_saved_was_screensaver ? "SCREENSAVER" : "STREAM");

    /* S4-05: gleicher Lock-Timeout-Fix wie in doorbell_start. Nach dem
     * Klingel-Ende ist der Stream-Canvas ggf. noch im Overlay (oder
     * wird gerade zurueckgereparented), der Refresh-Task ist busy.
     * 50ms reichten unter Last nicht -> scr_ringing_hide wurde nie
     * aufgerufen, Overlay haengt sichtbar. */
    if (bsp_display_lock(2000)) {
        scr_ringing_hide();  /* instant hide + canvas detach (S4-03) */

        /* S4-01: Modus restoren. Wenn der User vor der Klingel im
         * Bildschirmschoner war, kehrt er dahin zurueck. Backlight
         * laeuft eh ueber den 500ms-Poll-Timer der bei SCREEN_OFF-
         * idle-mode + abgelaufenem Timeout wieder ausblendet. */
        if (s_doorbell_saved_was_screensaver) {
            scr_idle_show_screensaver_mode();
        }
        /* Wenn vor dem Anruf STREAM war, ist scr_idle bereits in STREAM
         * (haben wir bei doorbell_start umgeschaltet). Kein Re-Switch
         * noetig. */

        start_fade(s_current_brightness, target);
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Doorbell end: display_lock(2000ms) TIMEOUT - overlay may persist!");
    }
}

backlight_reason_t idle_mode_mgr_current_reason(void)
{
    if (!s_mutex) return BACKLIGHT_REASON_IDLE;
    backlight_reason_t r;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return BACKLIGHT_REASON_IDLE;
    }
    r = s_reason;
    xSemaphoreGive(s_mutex);
    return r;
}

/* ---------------- Polling timer (LVGL-context) ---------------- */

static void poll_timer_cb(lv_timer_t *t)
{
    (void)t;

    idle_view_mode_t mode;
    int auto_ss_sec, screen_off_sec, brightness_idle;
    backlight_reason_t reason;
    bool screen_off;

    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) {
        /* Skip this tick rather than block the LVGL task. */
        return;
    }
    mode = s_mode;
    auto_ss_sec = s_auto_screensaver_sec;
    screen_off_sec = s_screen_off_after_sec;
    brightness_idle = s_brightness_idle;
    reason = s_reason;
    screen_off = s_screen_is_off;
    xSemaphoreGive(s_mutex);

    /* Klingel hat Vorrang - nichts tun. */
    if (reason == BACKLIGHT_REASON_DOORBELL) {
        return;
    }

    /* Touch via LVGL erkennen: wenn inactive_time < unser interner
     * inactive_us heisst das, LVGL hat einen Touch gesehen den wir
     * noch nicht reflektiert haben -> internen Timer mit-syncen. */
    uint32_t lvgl_inactive_ms = lv_display_get_inactive_time(NULL);
    int64_t our_inactive_us = now_us() - s_last_activity_us;
    uint32_t our_inactive_ms = (uint32_t)(our_inactive_us / 1000);

    if (lvgl_inactive_ms < our_inactive_ms) {
        /* LVGL sieht juengeren Touch. Update internen Timer und ggf
         * Display aufwecken. */
        if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
            s_last_activity_us = now_us() - (int64_t)lvgl_inactive_ms * 1000;
            xSemaphoreGive(s_mutex);
        }

        if (screen_off) {
            wake_screen_internal(brightness_idle);
            screen_off = false;
            if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
                s_screen_is_off = false;
                xSemaphoreGive(s_mutex);
            }
        }
        our_inactive_ms = lvgl_inactive_ms;
    }

    /* Mode-spezifische Logik */
    switch (mode) {
    case IDLE_MODE_SCREENSAVER:
        /* Backlight bleibt immer an - safety: falls aus, weck auf. */
        if (screen_off) {
            wake_screen_internal(brightness_idle);
            if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
                s_screen_is_off = false;
                xSemaphoreGive(s_mutex);
            }
        }
        /* Auto-Switch zurueck zu Bildschirmschoner-View */
        if (auto_ss_sec > 0 && our_inactive_ms >= (uint32_t)(auto_ss_sec * 1000)) {
            scr_idle_show_screensaver_mode();  /* idempotent */
        }
        break;

    case IDLE_MODE_LIVESTREAM:
        if (screen_off) {
            wake_screen_internal(brightness_idle);
            if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
                s_screen_is_off = false;
                xSemaphoreGive(s_mutex);
            }
        }
        /* Auto-Switch zurueck zu Stream-View */
        if (auto_ss_sec > 0 && our_inactive_ms >= (uint32_t)(auto_ss_sec * 1000)) {
            scr_idle_show_stream_mode();  /* idempotent */
        }
        break;

    case IDLE_MODE_SCREEN_OFF:
        /* Backlight nach Timeout aus. Kein Mode-Switch. */
        if (screen_off_sec > 0 && !screen_off &&
            our_inactive_ms >= (uint32_t)(screen_off_sec * 1000)) {
            sleep_screen_internal();
            if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
                s_screen_is_off = true;
                xSemaphoreGive(s_mutex);
            }
        }
        break;

    default:
        break;
    }
}

/* ---------------- Start ---------------- */

esp_err_t idle_mode_mgr_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    s_last_activity_us = now_us();
    s_reason = BACKLIGHT_REASON_IDLE;
    s_screen_is_off = false;

    /* Initial-Backlight auf brightness_idle setzen. */
    apply_backlight(s_brightness_idle);

    /* LVGL-Timer im LVGL-Task. Lock noetig fuer Erzeugung. */
    if (!bsp_display_lock(200)) {
        ESP_LOGE(TAG, "display_lock for timer create failed");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_poll_timer = lv_timer_create(poll_timer_cb, POLL_INTERVAL_MS, NULL);
    bsp_display_unlock();

    if (!s_poll_timer) {
        ESP_LOGE(TAG, "timer create failed");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "Started (initial brightness=%d, poll=%dms)",
             s_brightness_idle, POLL_INTERVAL_MS);
    return ESP_OK;
}
