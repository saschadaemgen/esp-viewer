/*
 * toast.h - Kurze Status-Toast-Anzeige
 *
 * Zeigt einen kleinen Banner unten im uebergebenen parent fuer 1.5 Sekunden,
 * dann fadet er weich aus. Mehrere Aufrufe in schneller Folge resetten
 * den Timer und ueberschreiben den Text.
 *
 * Beispiel:
 *   toast_init(parent);
 *   toast_show("Gespeichert");
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialisiert das Toast-Overlay als versteckten Container im parent.
 * Muss EINMAL pro Eltern-Container aufgerufen werden, bevor toast_show
 * gerufen wird.
 *
 * @param parent  Container in dem der Toast erscheint (z.B. root des
 *                Settings-Screens oder das Idle-Screen-Stage).
 */
void toast_init(lv_obj_t *parent);

/**
 * Zeigt den Toast mit der gegebenen Message fuer ~1.5 Sekunden.
 * Idempotent - Wiederholungsaufrufe resetten den Timer.
 */
void toast_show(const char *message);

#ifdef __cplusplus
}
#endif
