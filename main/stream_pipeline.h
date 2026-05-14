/*
 * stream_pipeline.h - MJPEG-Stream-Receiver Pipeline
 *
 * KOENIGSLIGA UniFi-Display, ESP-Saison 2 Tag 1
 *
 * Diese Komponente kapselt die komplette Saison-1-MJPEG-Pipeline:
 *   - JPEG-Decoder-Engine (Hardware, esp_driver_jpeg)
 *   - HTTP-MJPEG-Receiver (multipart/x-mixed-replace Parser)
 *   - LVGL-Canvas-Anzeige
 *
 * Saison 2 Spaeter:
 *   - stream_pipeline_stop() + _start() je nach FSM-State
 *   - Konfigurierbarer Host/Port/Path via /esp/config
 *   - Bearer-Token-Header
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Startet die MJPEG-Stream-Pipeline.
 *
 * Voraussetzung: WLAN-Verbindung steht (IP_EVENT_STA_GOT_IP).
 *
 * Verhalten:
 *   - Allokiert JPEG-In-Buffer (1 MB) und Canvas-Out-Buffer (800x1280 RGB565)
 *   - Initialisiert JPEG-Decoder-Engine
 *   - Loescht den aktuellen LVGL-Screen und legt ein Video-Canvas drauf
 *   - Spawned eine FreeRTOS-Task auf Core 0 die forever den Stream
 *     holt, dekodiert und ans Canvas zeichnet
 *
 * Diese Funktion blockiert NICHT - sie spawned nur die Task und kehrt zurueck.
 */
void stream_pipeline_start(void);

#ifdef __cplusplus
}
#endif
