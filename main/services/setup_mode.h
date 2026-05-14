/*
 * setup_mode.h - Setup-Modus fuer Token-Provisioning via Serial-Console
 *
 * ESP-Saison 2 Tag 2
 *
 * Wenn beim Boot der NVS-Slot device_token leer ist, wird der
 * Setup-Modus aktiviert. Der Setup-Modus:
 *   - Zeigt im Display einen Hinweis "Provisioning required"
 *   - Liest von der Serial-Console einen Token-String
 *   - Speichert den Token in NVS
 *   - Restartet den ESP, sodass beim naechsten Boot der
 *     normale Betrieb starten kann
 *
 * Saison 2 Spaeter (Tag 15+):
 *   - ESP-AP-Mode + Smartphone-Browser (Plug-and-Play)
 *   - UDP-Discovery + Long-Poll-Adoption
 *
 * Aktuell (Tag 2-14):
 *   - Serial-Console-basiert
 *   - Knutsche-Hase paste-t Token in `idf.py monitor`
 *   - Schnell, einfach, Marathon-tauglich
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Startet den Setup-Modus.
 *
 * Diese Funktion BLOCKIERT bis ein gueltiger Token empfangen wurde.
 * Nach erfolgreichem Empfang wird der Token in NVS gespeichert
 * und esp_restart() aufgerufen - die Funktion kehrt also normal NICHT
 * zurueck.
 *
 * Voraussetzung:
 *   - NVS muss initialisiert sein (nvs_flash_init)
 *   - UART0 muss verfuegbar sein (Standard auf JC8012P4A1C)
 *
 * Anzeige:
 *   - ESP_LOGI-Hinweise an Console
 *   - Optional: LVGL-Display-Hinweis (falls UI bereits init)
 */
void setup_mode_run(void);

#ifdef __cplusplus
}
#endif
