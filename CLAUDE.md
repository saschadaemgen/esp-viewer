# CLAUDE.md - display_app Repository

**Repo:** ESP32-P4 indoor monitor firmware
**Project:** KOENIGSLIGA / unifix indoor monitor (user brand: CARVILON)
**Last updated:** 24 May 2026, end of ESP season 5 (render breakthrough + doorbell design)
**Track:** ESP chat (hardware/firmware) - separate from the unifix-server repo

> Language policy: all source, comments and documentation are English. The
> chat workflow and the current device UI are German.

---

## 1. Overview

display_app is the ESP32-P4 firmware for the third-party indoor monitor in a
UniFi Access multi-tenant building installation. It runs on the GUITION
JC8012P4A1 hardware (ESP32-P4 + ESP32-C6-MINI-1U + 10.1" display) and connects
to the central unifix server (Go backend) on a per-installation Raspberry Pi.

Three-layer architecture:

```
Layer 3: main.c                     App coordinator
                                     boot flow, Wi-Fi, screen switching

Layer 2: ui/                        Presentation layer
                                     scr_loading, scr_idle, scr_ringing
                                     Lucide icons, animations

Layer 1: services/ + stream_pipeline.c   Network layer
                                     device_token (NVS)
                                     wifi_config (NVS)
                                     unifix_client (HTTP)
                                     sse_client (SSE doorbell push)
                                     stream_pipeline (MJPEG)
```

---

## 2. Hardware

```
MAIN HARDWARE:
   Board:        GUITION JC8012P4A1C_I_W_Y1
   Main MCU:     ESP32-P4 v1.3 (RISC-V dual-core 360 MHz)
   Coprocessor: ESP32-C6-MINI-1U-N4 (Wi-Fi + BT, over SDIO)
                 U variant with EXTERNAL antenna (u.FL connector).
   Display:     10.1" JD9365 IPS 800x1280 portrait, MIPI-DSI 2-lane
   Touch:       GSL3680 10-point (driver runs, events not yet wired)
   Audio:       ES8311 codec, onboard mic, speaker header (unused)
   PSRAM:       32 MB Hex @ 200 MHz
   Flash:       16 MB QIO 80 MHz Boya
   Power:       USB-C (production: PoE splitter)

BACKUP HARDWARE:
   Board:        GUITION JC4880P443C_I_W_Y1
   Identical SoC combo, smaller 4.3" display

DEV NETWORK (Recklinghausen):
   UDM SE:           192.168.1.1
   UA Intercom:      192.168.1.249 (G3 doorbell)
   Raspberry Pi 4:   192.168.1.42 (hostname `unifix`)
   ESP32-P4:         192.168.1.28  MAC 80:f1:b2:d0:b5:36
                     Wi-Fi: SONCLOUD
```

---

## 3. Software Stack

```
HOST PC (Windows):
   ESP-IDF v5.5.2
   Path: C:\Espressif\frameworks\esp-idf-v5.5.2
   Project: C:\Projects\UniFi\firmware\display_app\
   Build env: idf5.5_py3.11_env (Python 3.11)

ESP32-P4 FIRMWARE:
   App stack:        custom 3-layer architecture
   GUI library:      LVGL 9.2.2, framebuffer RGB565 (DPI panel, 2 FBs)
   Display BSP:      esp32_p4_function_ev_board (also works for JC8012P4A1)
   Display driver:   esp_lcd_jd9365 v1.0.2
   JPEG decoder:     esp_driver_jpeg (hardware) -> RGB565 (matches DPI FB)
   Wi-Fi:            esp-hosted v2.12.0 (host) + v2.12.7 (C6 slave)

RASPBERRY PI:
   Stream server on :8555 (MJPEG, auth-free)
   unifix-server Go binary (separate repo, master chat) on :9080
```

---

## 4. Directory Structure

```
display_app/
   main/
      main.c                          App coordinator + PPA init
      CMakeLists.txt                  all submodules registered
      stream_pipeline.c               MJPEG receiver, direct-FB render
      stream_pipeline.h
      services/                       Network layer
         device_token.c               read token from NVS
         wifi_config.c                Wi-Fi credentials from NVS
         setup_mode.c                 console-paste fallback on empty NVS
         unifix_client.c              HTTP client with bearer auth
         sse_client.c                 long-lived SSE raw socket
      ui/                             Presentation layer
         ui_animations.c              screen crossfade
         ui_icons.c                   icon glyph mapping
         lucide_22.c                  LVGL font 22px action icons
         lucide_88.c                  LVGL font 88px hero icons
         scr_loading.c                boot screen with glow
         scr_idle.c                   stream view: topbar + frame + actions
         scr_ringing.c                doorbell screen (header + video + toolbar)
         scr_screensaver.c            pixel clock + weather
         scr_settings.c               5+1 sections, auto-save
         idle_mode_mgr.c              backlight / view-mode control
         ui_tokens.h                  design tokens
   tools/
      provision-esp.py                NVS bulk provisioning
   partitions.csv                     NVS region 0x9000 / 0x6000
   sdkconfig                          DIRECT_MODE, num_fbs=2, RGB565
   .gitignore                         build/, .claude/, sdkconfig.old
```

---

## 5. Key Code Paths

### 5.1 NVS Layout

```
Namespace: "unifix"
   device_token   bearer token, length 43
   wifi_ssid      Wi-Fi SSID
   wifi_pwd       Wi-Fi password

Provisioning via tools/provision-esp.py:
   python tools\provision-esp.py <TOKEN> --ssid SONCLOUD --password "<PWD>" --port COM5 --erase
```

### 5.2 Boot Flow

```
1. NVS token check (setup mode if empty)
2. Display + LVGL init via bsp_display_start_with_config
3. show scr_loading (glow animation)
4. PPA init
5. build screens (idle/ringing/settings/screensaver)
6. Wi-Fi connect via esp-hosted -> ESP32-C6
7. get IP, "Probing /esp/heartbeat"
8. config fetch + cache
9. start SSE listener on /esp/events
10. start stream pipeline (direct-FB render into both FBs)
11. crossfade loading -> idle (400ms)
12. on SSE doorbell.ring: switch to ringing screen
```

### 5.3 Stream Pipeline

```
File:    main/stream_pipeline.c
Render:  direct-FB (since S5) - NO lv_canvas anymore
Pipeline:
   1. TCP socket to the stream server (RPi)
   2. HTTP GET with Accept: multipart/x-mixed-replace
   3. parse multipart boundary ('frame')
   4. read_next_frame() reads JPEG into s_jpeg_buf
   5. backlog skip logic
   6. JPEG decode (hardware) -> RGB565
   7. esp_async_fbcpy into BOTH DPI framebuffers (+ copy_done_sem wait)

Stream source (S5): RPi 192.168.1.42:8555
   Endpoint /api/stream.mjpeg?src=mjpeg_bal  (auth-free, 15 fps @ 800x1280)
   Health check: curl http://192.168.1.42:8555/api/profiles

RENDER RULES (hard-won, S5 - do NOT violate):
   - num_fbs=2, write the stream into BOTH FBs (else ghost image)
   - one pixel region = ONE writer. NO PPA UI overlay over the stream
     (double render with LVGL = flicker). UI belongs in stream-free zones
     (top header / bottom toolbar).
   - NO continuous animations in direct-FB (per-frame PSRAM buffer sync
     saturates bandwidth -> fps stall, LVGL #6118)
   - NO transform_rotate/scale (full-screen SW render = CPU killer)

Buffer allocation:
   JPEG decoder input  1 MB PSRAM
   stream buffer RGB565 ~2 MB PSRAM
   recv buffer        32 KB PSRAM
```

### 5.4 SSE Client

```
File:    main/services/sse_client.c
IMPORTANT: does NOT use esp_http_client (cannot hold long-stream connections).
Pipeline:
   1. raw socket connect to unifix server (port 9080 default)
   2. HTTP GET with Authorization: Bearer + Accept: text/event-stream
   3. chunked-decode loop, dispatch events (heartbeat, doorbell.ring,
      doorbell.cancel, unread_count, config.changed)
```

---

## 6. Doorbell Screen (S5 final, plan B)

```
Layout while ringing:
   y = 0 .. ~88     top header (idle-topbar style): "ES KLINGELT / <door>"
   y = 88 .. TB     full-width video (stream region)
   y = TB .. 1280   bottom action toolbar (safe area)

Toolbar buttons (left to right), vertically centered, STATIC (no anim):
   [MUTE]  ...  [Accept] [DOOR (largest, blue)] [Reject]  ...  [REC]
   MUTE + REC edge-aligned, separated by hairlines.

Door button uses the idle accent-blue tokens + ICON_LOCK_OPEN.
Accept neutral glass; reject neutral glass with red X icon only.
MUTE = ICON_BELL_OFF, REC = ICON_CIRCLE (red).
Note: UI label strings are still German (device UI not yet localized).
```

---

## 7. sdkconfig Anchors (do not change accidentally)

```
LVGL 9.2.2 (do NOT go to 9.5 - overflows IRAM at link)
LV_DRAW_THREAD_STACK_SIZE = 12288   (32768 -> Wi-Fi OOM crash)
LV_DRAW_BUF_ALIGN = 128             (L2 cache line; allocate 128-aligned)
DPI buffer mode: DIRECT_MODE (not FULL_REFRESH)
DPI framebuffers: num_fbs=2 (NOT 3 - 3 causes ghost image, S5)
Framebuffer format: RGB565
Shadow/circle cache on, LVGL demos off
Do not change version numbers in config files without approval.
```

---

## 8. Font Refresh (Lucide)

```
1. Get the codepoint from the OFFICIAL source, do not guess:
   https://unpkg.com/lucide-static@latest/font/lucide.css
   (e.g. S5: circle = E076, NOT E069 - E069 was a chart/bars glyph!)
2. Generate via lvgl.io/tools/fontconverter: name lucide_22, size 22, bpp 4,
   C file, compression OFF, lucide.ttf, Range = full codepoint list.
3. Replace main/ui/lucide_22.c.
4. The online converter adds a `.static_bitmap` field that LVGL 9.2.2 does
   not know -> align the struct format to a working font (e.g. montserrat_22.c),
   do not blindly delete the line (field order matters, else icons invisible).
5. Add the #define in lucide_22.h.
6. Verify: build compiles AND the icon shows correctly on device.
```

---

## 9. Diagnostic Tools (in code, S4/S5)

```
Task CPU logger (stats_log task, every 5s vTaskGetRunTimeStats):
   honest per-task CPU. Do NOT use the display percentage readout.
Boot diag log: "dpi fbs: fb0=.. fb1=.. (same=?)" - rules out FB pointer bugs.
Note: device debug fps = RENDER rate, NOT video fps (video fps = server fps).
```

---

## 10. Build / Deployment

```
Env idf5.5_py3.11_env via export.ps1 mandatory. On mismatch: idf.py fullclean.
NEVER erase-flash without approval (NVS token).
Flash: idf.py -p COM5 flash monitor
On stack/buffer bumps ALWAYS check `heap_init ... RAM` in the boot log
(Wi-Fi + LVGL share internal RAM).
```

---

## 11. Working With the Master Chat (unifix-server)

```
unifix-server repo (separate) provides the server endpoints:

ESP API (all bearer auth):
   GET  /esp/heartbeat         liveness check
   GET  /esp/events            SSE doorbell push
   POST /esp/reject            reject doorbell
   POST /esp/unlock            open door
   GET  /esp/config            UI config block
   GET  /esp/weather           weather
   GET  /esp/unread-count      unread count
   GET  /esp/history.json      history (S6 ESP feature)

Stream server (separate, :8555): /api/stream.mjpeg?src=mjpeg_bal (auth-free)

Provisioning on RPi: token from unifix admin (length 43), handed to
   python tools/provision-esp.py
```

---

## 12. Performance Benchmarks (end of season 5)

```
IDLE LIVESTREAM (direct-FB, RGB565, num_fbs=2):
   render fps:     ~50
   CPU:            ~5% (mjpeg task), ghost-free
   JPEG decode:    ~10ms (hardware)
   server fps:     mjpeg_bal 15 fps @ 800x1280 (RPi :8555)

RINGING (static, no anim):
   fps:            smooth (~25 render range), CPU ~10%
   ANIM WARNING:   bg_opa pulse in direct-FB -> 4 fps (PSRAM stall) = FORBIDDEN

IMPORTANT: device debug fps is the RENDER rate, not the video fps.
   Visual smoothness depends on the server fps.

BOOT TIME:
   to Wi-Fi:        ~7 s
   to idle screen:  ~9-10 s
```

---

## 13. Season History

```
SEASON 1 (10 May 2026):
   - pivot from snapshot to MJPEG, 11 fps stable, go2rtc+ffmpeg. HEAD 814a867

SEASON 2 (15-16 May 2026):
   - three-layer architecture, NVS provisioning, SSE, Lucide icons,
     UI screens. HEAD 6362fd9.

SEASON 3 (17-20 May 2026):
   - web-viewer parity: idle_mode_mgr, settings auto-save, screensaver
     (pixel clock + weather), doorbell reject, door unlock. Claude Code workflow.

SEASON 4 (21 May 2026):
   - INSIGHT SEASON: root cause of stutter = lv_canvas compositing
     (~3MB/frame over CPU), NOT the decode. Direct-FB path prototyped,
     rollback to original (5675273). RGB888 -> RGB565 correction.

SEASON 5 (22-24 May 2026):
   - RENDER BREAKTHROUGH. GHOST FIX: num_fbs 3->2 + stream into both FBs,
     ~50 fps ghost-free. CPU killer removed (transform anim). Doorbell-overlay
     flicker proven to be DOUBLE RENDER (LVGL vs PPA) -> bypassed via plan B
     (UI in stream-free zones). Doorbell screen finalized in the Apple/idle
     style (glass buttons, blue door, MUTE/REC). Font lucide_22 extended
     (bell-off E05A, circle E076). Branch perf/s5-stream-directfb.

SEASON 6 (planned):
   - investigate blue reset flash (~5 min)
   - large refactoring (every file; remove German comments -> English;
     secure non-VCS patches; remove dead PPA-overlay code)
   - migrate all legacy docs/season protocols from German to English
   - MUTE (back to idle) + REC (audio recording, ES8311)
   - rounded idle corners (clean redo, not the PPA corner-mask path)
   - history view (coordinate with master chat)
```

---

*End of CLAUDE.md for display_app. Last updated 24 May 2026, end of ESP season 5.*
