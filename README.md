# esp-viewer

ESP32-P4 Innenmonitor-Firmware für UniFi-Access-Intercom-Anlagen.

Teil der **unifix**-Plattform: ein privates Mehrfamilienhaus-System, das auf UniFi-Access-Hardware aufsetzt und Mietern Klingel-Empfang, Live-Stream und Tür-Öffnung über ein 10.1"-Touchscreen-Display zur Verfügung stellt. Der Server-Anteil (Go) lebt in einem separaten Repo, dieses Repo ist ausschließlich die ESP32-Firmware.

## Hardware

- **Board:** GUITION JC8012P4A1C_I_W_Y1
- **Main-MCU:** ESP32-P4 v1.3 (RISC-V Dual-Core 360 MHz)
- **Coprozessor:** ESP32-C6-MINI-1U-N4 (WLAN + BT via SDIO, externe Antenne via u.FL)
- **Display:** 10.1" JD9365 IPS 800x1280 Portrait, MIPI-DSI 2-Lane
- **Touch:** GSL3680 10-Punkt
- **PSRAM:** 32 MB Hex @ 200 MHz
- **Flash:** 16 MB QIO 80 MHz

Backup-Hardware: GUITION JC4880P443C_I_W_Y1 (4.3"), identische SoC-Combo.

## Architektur

Drei-Schichten-Aufbau:

```
main.c                     App-Coordinator, Boot-Flow, Screen-Switching
ui/                        Presentation-Layer (LVGL 9.2.2, RGB888 24-Bit)
services/                  Network-Layer
   device_token             Bearer-Token aus NVS
   wifi_config              WLAN-Credentials aus NVS
   setup_mode               Console-Paste-Fallback bei leerer NVS
   unifix_client            HTTP-Client (heartbeat / reject / unlock)
   sse_client               SSE-Long-Stream auf /esp/events
stream_pipeline.c          MJPEG-Receiver mit FIONREAD-Skip
```

Alle Server-Kommunikation läuft über Bearer-Auth gegen einen unifix-Server (Go-Backend auf Raspberry Pi 4, separate Codebase).

## Voraussetzungen

- ESP-IDF v5.5.2 (Path: `C:\Espressif\frameworks\esp-idf-v5.5.2`)
- GUITION-Board mit korrekt angeschlossener externer C6-Antenne (ohne Antenne unbrauchbar)
- Erreichbarer unifix-Server im LAN
- Ein gültiges Device-Token aus dem unifix-CLI (`unifix-cli esp adopt`)

## Provisioning

Beim ersten Flashen ist NVS leer. Das Gerät startet in den Setup-Modus und nimmt Token plus WLAN-Credentials per Konsole-Paste oder Provisioning-Tool entgegen.

Schneller Weg via Skript:

```powershell
python tools\provision-esp.py <TOKEN> --ssid <SSID> --password "<PWD>" --port COM5 --erase
```

Das Skript erased den Flash, schreibt drei NVS-Keys (`device_token`, `wifi_ssid`, `wifi_pwd`) und bricht ab. Anschließend kann die Firmware geflasht werden.

## Build und Flash

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

Beim ersten Boot:

1. NVS-Token-Prüfung → falls leer, Setup-Modus
2. WLAN-Connect (ca. 7s)
3. Heartbeat-Probe gegen `/esp/heartbeat`
4. SSE-Listener auf `/esp/events`
5. Stream-Pipeline auf `/esp/stream.mjpeg`
6. Fade zu Idle-Screen

## Verzeichnis-Struktur

```
display_app/
├── main/                  Application-Code
│   ├── main.c             Coordinator
│   ├── stream_pipeline.c  MJPEG-Receiver
│   ├── services/          Netzwerk + NVS
│   └── ui/                Screens, Animations, Icons
├── tools/
│   └── provision-esp.py   NVS-Bulk-Provisioning
├── partitions.csv
├── sdkconfig
└── CMakeLists.txt
```

## Status

Aktuell durch und committed:

- **Saison 1:** MJPEG-Stream via go2rtc + ffmpeg, 11 FPS stabil
- **Saison 2:** Drei-Schichten-Architektur, RGB888-Migration, NVS-Provisioning, SSE-Listener, Lucide-Icon-Fonts, Klingel-Overlay-UI
- **Saison 3:** Klingel-Ablehnen (POST /esp/reject), Tür-Öffnen (POST /esp/unlock), Stream-Umstellung auf unifix-Proxy mit Bearer-Auth

In Planung: Annehmen-Flow mit bidirektionalem Audio, `/esp/config`-Konsumption, eigene Interface-Designer-Integration.

## Bekannte Stolperfallen

- **C6 ohne externe Antenne:** Symptom -76 dBm bei 1 m statt -45 dBm. Kein nutzbares Streaming. Vor jeder Fehlersuche prüfen.
- **C6-Firmware-Mismatch-Warnung:** Nicht reflexartig updaten. Erst prüfen ob es tatsächlich Probleme verursacht. OTA-Update-Fehler können das Gerät in Endlosschleife versetzen.
- **`esp_http_client` für SSE ungeeignet:** Long-Lived Connections schlagen fehl. `sse_client.c` nutzt deshalb Raw-Socket.
- **LVGL 9.2.x `lv_grad_radial_init`:** Smooth-Bug. Workaround via PNG-Asset.

## Lizenz

Privat, kein Open-Source-Release vorgesehen.

## Repos

- **unifix-server** (Go-Backend): separates privates Repo
- **esp-viewer** (dieses Repo): ESP32-Firmware
