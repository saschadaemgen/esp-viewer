/*
 * stream_pipeline.h - MJPEG-Stream-Receiver Pipeline
 *
 * KOENIGSLIGA UniFi-Display
 *
 * Encapsulates the complete Saison-1 MJPEG pipeline:
 *   - JPEG decoder engine (hardware, esp_driver_jpeg)
 *   - HTTP MJPEG receiver (multipart/x-mixed-replace parser)
 *   - LVGL canvas display
 *
 * Saison 2 update: canvas is created inside a caller-provided
 * parent object (the .stream slot of the idle screen) so the
 * video frames render inside the web-viewer-style frame.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the MJPEG stream pipeline.
 *
 * Preconditions:
 *   - WLAN connection up (IP_EVENT_STA_GOT_IP fired)
 *   - LVGL display initialised
 *
 * Behaviour:
 *   - Allocates JPEG input buffer (1 MB) and canvas output buffer
 *     (800x1280 RGB565)
 *   - Initialises hardware JPEG decoder engine
 *   - Creates an lv_canvas as child of `parent` filling its area
 *   - Spawns a FreeRTOS task on core 0 that fetches, decodes and
 *     blits frames to the canvas forever
 *
 * @param parent  LVGL parent object the canvas is created in.
 *                Typically the .stream slot returned from
 *                scr_idle_build(). If NULL, lv_screen_active()
 *                is used as fallback (Saison-1 behaviour).
 */
void stream_pipeline_start(lv_obj_t *parent);

/**
 * Temporaer den Stream-Canvas in einen anderen Eltern-Container
 * verschieben (Klingel-Overlay).
 *
 * Verwendung (S4-03): waehrend der Klingel-Anzeige wird der existierende
 * Single-Canvas ins fullscreen Klingel-Overlay reparented, damit der
 * Live-Stream als Vollbild-Hintergrund hinter Bell + Buttons sichtbar
 * wird. Beim Klingel-Ende wird er via stream_pipeline_detach_from_overlay
 * zurueckgesetzt.
 *
 * Caller-Verantwortung:
 * - Z-Order: der Canvas wird per lv_obj_set_parent als LETZTES Kind im
 *   neuen Parent angehaengt. Wer eine bestimmte Z-Position braucht,
 *   ruft lv_obj_move_to_index() auf dem Return-Pointer auf.
 * - Wenn der Pipeline-Task noch nicht oder nicht mehr lebt (kein Canvas
 *   erzeugt), gibt die Funktion NULL zurueck und macht nichts.
 *
 * Thread-Safety: nimmt intern bsp_display_lock(100). Recursive-Mutex
 * macht nested-locking von einem bereits gelockten Caller safe.
 *
 * @param new_parent  Neuer Parent-Container (z.B. das Ringing-Overlay).
 *                    NULL ist No-Op und gibt NULL zurueck.
 *
 * @return Pointer auf den Canvas (zum optionalen Z-Order-Positionieren)
 *         oder NULL bei Fehler / no-op.
 */
lv_obj_t *stream_pipeline_attach_to_overlay(lv_obj_t *new_parent);

/**
 * Den Stream-Canvas zurueck zu seinem Original-Parent (stream_view in
 * scr_idle) verschieben. KRITISCH: muss auf JEDEM Klingel-Schliess-Pfad
 * laufen (cancel / reject / accept), sonst bleibt der Canvas Kind eines
 * versteckten Overlays und der Idle-Stream wird nach der ersten Klingel
 * schwarz.
 *
 * Thread-Safety: nimmt intern bsp_display_lock(100).
 */
void stream_pipeline_detach_from_overlay(void);

#ifdef __cplusplus
}
#endif
