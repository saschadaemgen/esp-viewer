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

#ifdef __cplusplus
}
#endif
