/*
 * stream_pipeline.h - MJPEG-Stream-Receiver Pipeline
 *
 * KOENIGSLIGA UniFi-Display
 *
 * S5-04 Direct-FB-Pfad:
 *   - JPEG decoder engine (hardware, esp_driver_jpeg)
 *   - HTTP MJPEG receiver (multipart/x-mixed-replace parser)
 *   - RGB565-Decode (matched DPI-FB-Format)
 *   - Direct write via esp_lcd_panel_draw_bitmap in die stream_view-
 *     Region des DPI-Framebuffers (Pointer-Swap, kein Kopieren).
 *   - KEIN lv_canvas-Compositing - LVGL beruehrt das Video nicht.
 *
 * Hintergrund: der lv_canvas-Pfad (S1-S5-03) hatte ~49% lvglDraw-CPU
 * + Bild ruckelig (Geraete-Messung S5-03). Direct-FB lief in S5-02 mit
 * ~10% CPU + 60 fps - dieser Pfad ist seither der einzige produktive.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * S5-07 FB-Sync installieren.
 *
 * Muss aufgerufen werden NACH bsp_display_start_with_config (esp_lvgl_port
 * hat dann seinen on_refresh_done-Callback registriert) und VOR
 * stream_pipeline_start (mjpeg-Task braucht die FB-Pointer und das
 * Copy-Done-Sema).
 *
 * Effekte:
 *   - holt via esp_lcd_dpi_panel_get_frame_buffer(panel, 2, ...) die
 *     beiden FB-Adressen die LVGL alterniert beschreibt
 *   - install eigener esp_async_fbcpy-Handle fuer DMA2D-Copies in die
 *     FBs (statt esp_lcd_panel_draw_bitmap das nur einen FB triffft)
 *   - registriert on_refresh_done-Wrapper der das lvgl-port-trans_sem
 *     weiterleitet (sonst haengt LVGL's flush_callback im
 *     blockierenden xSemaphoreTake)
 *
 * Voraussetzungen: BSP_LCD_DPI_BUFFER_NUMS=2, AVOID_TEAR=y, DIRECT_MODE=y.
 * Lokaler Patch in esp_lvgl_port (lvgl_port_get_trans_sem) bleibt
 * Voraussetzung - siehe S5-05/S5-06.
 *
 * @return ESP_OK, sonst Fehlercode.
 */
esp_err_t stream_pipeline_install_fb_sync(void);

/**
 * Set the visibility gate for the direct-FB render path.
 *
 *   true  -> mjpeg_task macht esp_lcd_panel_draw_bitmap nach jedem
 *            Decode (Stream-Pixel landen in der Stream-Region des
 *            Panel-Framebuffers).
 *   false -> Decode laeuft weiter (TCP-Backlog leer halten), aber der
 *            Draw wird uebersprungen. Stream-Pixel landen NICHT im FB
 *            wenn der Livestream-View nicht aktiv ist (Screensaver,
 *            Settings).
 *
 * Aufrufer:
 *   scr_idle_show_stream_mode      -> true  (nicht wenn Settings offen)
 *   scr_idle_show_screensaver_mode -> false
 *   scr_idle_show_settings         -> false
 *   scr_idle_show_stream (close S) -> true wenn current_mode=STREAM
 *   scr_ringing_show               -> true  (Stream bleibt im Klingel)
 *   scr_ringing_hide               -> abhaengig vom Idle-Mode-Restore
 *
 * Thread-Safety: volatile bool, Aufruf aus jedem Task ok.
 */
void stream_pipeline_set_visible(bool visible);

/**
 * Start the MJPEG stream pipeline.
 *
 * Preconditions:
 *   - WLAN connection up (IP_EVENT_STA_GOT_IP fired)
 *   - LVGL display initialised (bsp_lcd_get_panel_handle() != NULL)
 *
 * Behaviour:
 *   - Allocates JPEG input buffer (1 MB) and decode output buffer
 *     (800x1280 RGB565, ~2 MB)
 *   - Initialises hardware JPEG decoder engine
 *   - Spawns a FreeRTOS task on core 0 that fetches, decodes and
 *     writes frames directly into the DPI-Framebuffer
 *
 * @param parent  Unbenutzt seit S5-04 (war frueher der lv_canvas-
 *                Parent). Caller kann NULL uebergeben. Signatur bleibt
 *                fuer Source-Kompatibilitaet mit main.c.
 */
void stream_pipeline_start(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
