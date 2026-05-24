# esp-viewer

![Platform](https://img.shields.io/badge/platform-ESP32--P4-E7352C?logo=espressif&logoColor=white)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.2-blue?logo=espressif&logoColor=white)
![LVGL](https://img.shields.io/badge/LVGL-9.2.2-2C8EBB)
![Display](https://img.shields.io/badge/display-800x1280%20MIPI--DSI-555)
![Color](https://img.shields.io/badge/framebuffer-RGB565-orange)
![Season](https://img.shields.io/badge/season-5%20complete-success)
![Build](https://img.shields.io/badge/build-passing-brightgreen)
![License](https://img.shields.io/badge/license-proprietary-lightgrey)
![Status](https://img.shields.io/badge/status-active%20development-blue)

ESP32-P4 indoor monitor firmware for UniFi Access intercom installations.

Part of the **unifix** platform (user-facing brand: **CARVILON**): a private
multi-tenant building system built on top of UniFi Access hardware that gives
tenants doorbell reception, live video and door unlocking on a 10.1" touch
display. The server side (Go) lives in a separate repository; this repo is the
ESP32 firmware only.

The ESP is the **indoor monitor (receiver/display)**, not the door station.
It receives an MJPEG stream prepared by the unifix server (Raspberry Pi),
decodes it via hardware JPEG and displays it, along with the doorbell, idle,
settings and screensaver UI. It does not send video.

> Note: project communication and the current device UI are in German. All
> source, comments and documentation are English.

---

## Table of Contents

- [Hardware](#hardware)
- [Architecture](#architecture)
- [Render Path](#render-path-the-core)
- [Requirements](#requirements)
- [Provisioning](#provisioning)
- [Build and Flash](#build-and-flash)
- [Directory Structure](#directory-structure)
- [Status](#status)
- [Known Pitfalls](#known-pitfalls)
- [Conventions](#conventions)
- [License](#license)

---

## Hardware

| Component       | Specification                                              |
|-----------------|------------------------------------------------------------|
| **Board**       | GUITION JC8012P4A1C_I_W_Y1                                  |
| **Main MCU**    | ESP32-P4 v1.3 (RISC-V dual-core 360 MHz)                  |
| **Coprocessor** | ESP32-C6-MINI-1U-N4 (Wi-Fi + BT over SDIO, external u.FL antenna) |
| **Display**     | 10.1" JD9365 IPS 800x1280 portrait, MIPI-DSI 2-lane        |
| **Touch**       | GSL3680 10-point                                          |
| **Audio**       | ES8311 codec, onboard mic, speaker header (unused)         |
| **PSRAM**       | 32 MB Hex @ 200 MHz                                        |
| **Flash**       | 16 MB QIO 80 MHz                                           |
| **Power**       | USB-C (production: PoE splitter)                          |

> **Backup hardware:** GUITION JC4880P443C_I_W_Y1 (4.3"), identical SoC combo.

---

## Architecture

Three-layer design with strict separation: `ui` knows no sockets, `services`
knows no LVGL, `main` wires both together.

```
main.c                     App coordinator: boot flow, LVGL init, PPA init,
                           build screens, start tasks, wire events
ui/                        Presentation layer (LVGL 9.2.2, RGB565)
   scr_loading              Boot screen with glow
   scr_idle                 Stream-view slot + topbar + action bar
   scr_ringing              Doorbell screen (full-width video + toolbar)
   scr_screensaver          Pixel clock + weather
   scr_settings             5+1 sections, auto-save
   idle_mode_mgr            Backlight / view-mode control
   lucide_22 / lucide_88    Icon fonts
services/                  Network layer
   device_token             Bearer token from NVS
   wifi_config              Wi-Fi credentials from NVS
   setup_mode               Console-paste fallback on empty NVS
   unifix_client            HTTP client (heartbeat / reject / unlock / config)
   sse_client               SSE long-stream on /esp/events (raw socket)
stream_pipeline.c          MJPEG receiver, HW JPEG decode, direct-FB render
```

All server communication uses bearer auth against a unifix server (Go backend
on a Raspberry Pi, separate codebase).

**Important distinction:** *adoption* is done by the device itself, *pairing*
(tenant association) is done by the admin in the UA/unifix UI. The monitor
does not need to handle pairing.

---

## Render Path (the core)

The stream is **not** rendered through an `lv_canvas` (that was the case up to
season 4 and cost ~3 MB/frame over the CPU). Since season 5 the pipeline
writes the decoded frame **directly into the DPI framebuffer**
(`esp_async_fbcpy`, RGB565), which drops idle-livestream CPU to ~5% at ~50 fps.

The hard-won render rules of this setup:

- **Two framebuffers (`num_fbs=2`), write the stream into BOTH.** The panel
  rotates through the FBs on VSYNC; if only one is written, moving objects
  appear as a ghost image. Every pixel region must be consistent across all
  active FBs.
- **One pixel region, one writer.** Blending UI over the stream via PPA
  collides with LVGL's own rendering of the same objects (double render ->
  flicker). The UI therefore lives in zones the stream does **not** write
  (top header, bottom toolbar) = pure LVGL, stable.
- **No continuous animations in direct-FB mode.** In LVGL DIRECT_MODE, LVGL
  syncs the dirty area between both FBs over PSRAM after every change; with a
  running animation this saturates PSRAM bandwidth per frame (competing with
  the video DMA) and the fps collapses.
- **Hardware JPEG is not the bottleneck** (~10 ms/frame, 720p@88fps per
  datasheet). The bottleneck was always compositing. H.264 decode on the P4
  is software-only (baseline, slow) -> MJPEG is the default.

The doorbell screen applies this principle: full-width video between a top
header and a bottom action toolbar, both in the safe (stream-free) area.

---

## Requirements

- **ESP-IDF v5.5.2** (path: `C:\Espressif\frameworks\esp-idf-v5.5.2`), env
  `idf5.5_py3.11_env` (Python 3.11).
- Reachable unifix server on the LAN.
- A valid device token from the unifix admin (token length 43).

---

## Provisioning

On first flash, NVS is empty. The device boots into setup mode and accepts the
token plus Wi-Fi credentials via console paste or the provisioning tool.

```powershell
python tools\provision-esp.py <TOKEN> --ssid <SSID> --password "<PWD>" --port COM5 --erase
```

The script writes three NVS keys (`device_token`, `wifi_ssid`, `wifi_pwd`).

> **WARNING: Never `erase-flash` during normal operation.** It wipes the NVS
> region holding the token and Wi-Fi credentials. Only the provisioning tool
> may do this deliberately.

---

## Build and Flash

```powershell
# Build environment (before every build)
C:\Espressif\frameworks\esp-idf-v5.5.2\export.ps1

# Build + flash + monitor
idf.py build
idf.py -p COM5 flash monitor
# Quit the monitor: Ctrl+]
```

On env mismatch or a stuck build:

```powershell
Get-Process idf.py,ninja,cmake,python -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -Recurse -Force build
idf.py fullclean
```

Healthy boot markers:

```
esp_psram: Found 32MB PSRAM device, Speed: 200MHz
sta ip: 192.168.1.28
Heartbeat OK - unifix server reachable
STREAM: dpi fbs: fb0=... fb1=... (same=0)   <- two real FBs
STREAM: Boundary: 'frame'
==== Task CPU stats ====                     <- every 5s, per-task CPU truth
```

First boot sequence:

1. NVS token check -> setup mode if empty
2. Wi-Fi connect (~7 s) via the C6
3. Heartbeat probe against `/esp/heartbeat`
4. SSE listener on `/esp/events`
5. Stream pipeline (direct-FB render)
6. Fade to idle screen

---

## Directory Structure

```
display_app/
|-- main/
|   |-- main.c                 App coordinator + PPA init
|   |-- stream_pipeline.c      MJPEG receiver, direct-FB render (both FBs)
|   |-- services/              Network + NVS
|   |   |-- device_token.c
|   |   |-- wifi_config.c
|   |   |-- setup_mode.c
|   |   |-- unifix_client.c
|   |   \-- sse_client.c       Raw-socket SSE
|   \-- ui/                    Screens, animations, icon fonts
|       |-- scr_idle.c         Design reference (glass buttons, hairlines)
|       |-- scr_ringing.c      Doorbell screen (header + video + toolbar)
|       |-- scr_screensaver.c
|       |-- scr_settings.c
|       |-- idle_mode_mgr.c
|       |-- lucide_22.c/.h     Icon font (bell-off, circle, etc.)
|       \-- ui_tokens.h        Design tokens (colors, spacing, radii)
|-- tools/
|   \-- provision-esp.py       NVS bulk provisioning
|-- partitions.csv
|-- sdkconfig.defaults
\-- CMakeLists.txt
```

---

## Status

| Season | State | Content |
|--------|-------|---------|
| **1** | done | MJPEG stream via go2rtc + ffmpeg, 11 fps stable |
| **2** | done | Three-layer architecture, NVS provisioning, SSE listener, Lucide fonts, doorbell overlay UI |
| **3** | done | Web-viewer parity: idle_mode_mgr, settings (auto-save), screensaver (pixel clock + weather), doorbell reject, door unlock |
| **4** | done | Insight season: root cause of stutter found (lv_canvas compositing, not decode). Direct-FB path prototyped |
| **5** | done | **Render breakthrough:** ghost fix (num_fbs 3->2, ~50 fps), CPU killer removed, doorbell-overlay flicker solved via plan B (UI in stream-free zones), doorbell screen finalized in the Apple/idle style |

**Planned (season 6+):**

- Accept-call flow with bidirectional audio (ES8311 codec present)
- Mute/ignore action (back to idle), REC call recording
- History view on the ESP (coordinated with the server backend)
- Investigate periodic blue reset flash (~5 min)
- Code refactoring and cleanup, incl. migrating German comments/docs to English

---

## Known Pitfalls

- **`esp_http_client` unsuitable for SSE:** long-lived connections fail.
  `sse_client.c` uses a raw socket instead.
- **LVGL 9.2.x `lv_grad_radial_init`:** smooth-gradient bug, workaround via
  asset.
- **Do not upgrade LVGL to 9.5:** overflows IRAM at link time.
- **`LV_DRAW_THREAD_STACK_SIZE`:** keep at 12288. 32768 starves Wi-Fi of
  internal RAM -> SDIO OOM crash.
- **No animations / no UI overlay in the stream region:** direct-FB setup,
  see [Render Path](#render-path-the-core).
- **Font refresh (Lucide):** the online converter now adds a `static_bitmap`
  field that does not compile against LVGL 9.2.2. When importing, align the
  struct format to a working font. Always verify icon codepoints against the
  official `lucide.css`, never guess.

---

## Conventions

- **Git:** Conventional Commits. Claude Code commits locally, never pushes;
  pushing is manual. Work directly on the working branch (no worktrees).
  `.claude/` is gitignored.
- **Language:** all source, comments and docs are English. The chat workflow
  and the current device UI are German.
- **LVGL** stays at 9.2.2. Do not change version numbers in config files
  without approval.
- **BSP** `esp32_p4_function_ev_board` is not VCS-tracked (lives locally).
  Note any local BSP getters in the season report.
- **Maxim:** don't guess, look it up. Marathon not sprint. Quality first.

---

## License

Proprietary. No open-source release planned.

---

## Repositories

- **unifix-server** (Go backend): separate private repo
- **esp-viewer** (this repo): ESP32 firmware
