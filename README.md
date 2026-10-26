# Pico Paint

A small paint application for the **Raspberry Pi Pico 2 W** with a **4" ILI9488 480×320 SPI TFT** and an **HR2046 / XPT2046-compatible resistive touch panel**.

The app runs in **portrait (320×480)** and is designed to work fully offline. Wi-Fi is only used for OTA updates.

**Using the device:** see the [Pico Paint user guide](docs/user-guide.md) for
the canvas, tools, settings, idle behaviour and recovery mode.

## Status

Milestones **M1–M3** are hardware-verified: serial banner and 2 MB LittleFS;
the ILI9488 driver with RGB565→RGB666 flush, correct colours, upright portrait
(rotation 2), stable 40 MHz SPI and a 139.3 ms full-screen flush; and the
XPT2046/HR2046 touch stack with 4-point affine calibration, debounced gestures,
an 800 ms splash, canvas, 48 px top bar, bottom sheets, toasts and placeholder
Gallery/Settings/About screens. Settings → **Calibrate** saves its
transform to `/config.bin` and reloads it on boot.

**M4 (drawing engine)** is implemented and **hardware-validated on
2026-10-24**. The normal build was exercised manually for drawing, undo and
clearing; the diagnostics build produced the timings below. It includes:

- A clipped RGB565 canvas over the main framebuffer, with dirty-rectangle
  tracking and direct canvas flushes.
- Round brush masks for 1/2/4/8/16/24/32 px, gap-free stroke interpolation,
  brush and eraser strokes.
- The 24-colour palette, five recent colours, and an HSV picker with a hue bar,
  saturation/value square, live preview and Cancel/Use.
- Compressed undo/redo: each action captures only the tiles it touches, stores
  an RLE or raw "before" record, converts it to an XOR delta at commit, and
  keeps up to 16 actions in a fixed pool. Typical strokes use a fraction of a
  full tile snapshot, so ten normal undo levels fit more often; the oldest
  action is evicted when the pool is full.
- **Menu → New canvas** asks for confirmation, clears the drawing and undo
  history, and keeps the toast/overlay paths pixel-safe.
- A 320×32 overlay scratch band so sheets, toasts, Settings, Gallery and
  calibration never destroy the canvas beneath them.

The current M8 environments build successfully (`pico2w_usb` RAM 164,692 B /
flash 505,940 B; `pico2w_ota` matches; `pico2w_diag` RAM 164,792 B / flash
507,204 B with the local `secrets.h`), and the native suite passes **75/75**.
M5 adds the line, rectangle, ellipse, flood fill and eyedropper tools with
rubber-band preview and full undo support. The tools sheet also has a
contextual options row: Size opens the brush-size sheet, and Outline/Filled
switches rectangle and ellipse commits. Filled previews show the boundary
while dragging. The M4 follow-ups are implemented too: a single undo action
can now use all 51 tile records, and HSV taps repaint only the affected marker
bounds with a 32×32 S/V painter.

The **M6 storage foundation (M6-1–M6-3)** is implemented and native-tested:
IEEE CRC-32, a strict 16-bit run/literal RLE codec, the `.pnt` v1 file format
with a 32-byte header and embedded 64×86 thumbnail, and atomic saves that write
a `.tmp` file, verify its size/CRC/RLE structure, and only then rename it over
the destination. `loadPnt()` verifies the header, exact dimensions and CRC
before touching canvas pixels.

The **save/load UI and gallery (M6-4/M6-5)** are implemented too. The menu now
has **New canvas, Save, Save as, Gallery, Settings, Wi-Fi & OTA, About**.
Save overwrites the current file (or opens Save as for a new drawing); Save as
proposes the next free `imgNNNN.pnt` and asks for confirmation. The Gallery is
a paged 2×2 grid with streamed thumbnails, filenames, an empty state and
long-press delete confirmation. Tapping a drawing opens a load confirmation
that warns when the current canvas has unsaved changes. Thumbnails are read
from LittleFS one 128 B row at a time, so the gallery does not keep four
11 KB thumbnail buffers in RAM.

**Autosave and boot recovery (M6-6)** are implemented. The first canvas change
arms a 15 s timer; additional changes do not postpone it, and an idle dirty
document is not rewritten until it changes again. Writes go atomically to
`/paint/autosave.pnt`, reuse the normal streaming save path, and wait while a
brush stroke or shape preview is active. At boot the autosave is validated with
the full header/dimensions/size/CRC/RLE checks; a valid file opens a modal
Recovery screen with **Restore** and **Discard**. Restore brings the pixels back
as an unnamed dirty document (the next Save opens Save as). A successful
Save/Save as, load, New canvas or explicit Discard removes the autosave. A
failed save or recovery keeps it for the next attempt.

**Storage gauge and OTA reserve (M6-7)** are in place. Settings shows free
space, a used-percentage bar and an **OTA reserve: OK/LOW** status. The app
keeps **700 KB (716,800 B)** free for firmware staging: the Wi-Fi & OTA entry
refuses to continue below that threshold, and a save that crosses it reports
`OTA low` while still preserving the drawing. The gauge refreshes on boot, when
Settings opens and after saves, deletes and autosaves. The update flow now
enforces the same reserve before it starts the OTA server.

**Settings persistence and storage diagnostics (M6-8/M6-9)** are implemented.
`/config.bin` is now a version 2 blob: rotation, brightness, OTA enable, feature
flags, the six calibration coefficients and a 16-byte last-opened filename.
Version 1 blobs still load and are upgraded on the next save. Successful
Save/Save as and load update the last-opened name; New, deleting the open file
and recovery Restore/Discard clear it. It is persisted for M8 but is not
auto-opened at boot. Settings → **Storage details** shows mount status, total,
used and free space, the real number of gallery pictures (including files
beyond the 32-entry index cap) and the OTA-reserve state.

**Wi-Fi and OTA (M7-1–M7-8; M7-12 security notes)** are implemented. The firmware runs a non-blocking
Wi-Fi state machine with a 10 s connect timeout, 30 s retries and an offline
fallback, so painting never waits for the network. `Menu → Wi-Fi & OTA` shows
the connection state, configured/missing credentials, IP address, hostname and
port, RSSI, OTA-server state, the 700 KB staging reserve and the last OTA error.
The screen also has an enable/disable toggle; the choice is persisted in
`Settings.otaEnabled`. Without `include/secrets.h` the SSID is empty, the
wireless stack is not started and the toggle is disabled.

`ArduinoOTA` listens on **port 2040** with the configured hostname and password.
Starting an update closes any open sheet, autosaves a dirty drawing, clears undo
history, and shows a progress overlay over the preserved canvas. `onProgress`
repaints the overlay directly because `ArduinoOTA.handle()` blocks during the
transfer; a failed update returns to the drawing with a toast, while a
successful one reboots. The overlay never composes paper over the canvas
framebuffer, so the current drawing survives a failed transfer. Authentication
errors can arrive in the lwIP context, so that callback only records an error
flag and the main loop performs all display access.

The OTA server is not started unless LittleFS has at least 700 KB free. The
updater then checks the incoming image against the staging file and reports
`OTA begin failed` if it cannot be created. The current image is about 506 KB,
so the reserve leaves room for the staged image plus the filesystem. The
static Wi-Fi/lwIP pools (16,403 B `ram_heap` + 36,771 B PBUF) are already
included in the current 164,692 B figure. The estimated boot heap is
31,916 B; the actual `[mem] free heap before net` / `[mem] free heap` values
still need recording on hardware.

**Settings, idle polish and recovery (M8-1–M8-9)** are implemented and
native-tested. Settings now has rotation (upright/flipped portrait), four
brightness steps, Wi-Fi/OTA enable, touch recalibration, the storage gauge,
About and Wi-Fi details. The backlight dims after 60 s and turns off after
5 min; the first touch while off only wakes the display. A splash logo/version
screen can be skipped by holding touch. Boot health is counted in
`/boot.health`; three boots that do not stay healthy for five seconds enter a
safe-mode screen with display, touch and storage self-tests. Holding touch at
power-on forces the same recovery mode. Safe mode skips Wi-Fi and autosave.
Error toasts cover storage mount failure, display init failure, low OTA reserve
and OTA failures, and the primary controls are 44 px targets with text status
labels rather than colour-only cues.

The M6 save/load/gallery, autosave, storage-gauge, config-migration and
power-cut flows were hardware-validated after the M6 build. M8 and Wi-Fi/OTA
hardware validation is still pending: rotation/touch mapping, brightness and
idle wake, persistence, safe-mode entry and self-tests, the boot-health counter,
ten consecutive updates, wrong-password, Wi-Fi-loss, power-loss and low-space
failure paths, USB recovery, and the live heap/RSSI/SPI-interaction
measurements. See [`BACKLOG.md`](BACKLOG.md) for the full plan,
[`docs/user-guide.md`](docs/user-guide.md) for day-to-day use,
[`docs/ota.md`](docs/ota.md) for the update workflow and
[`docs/performance.md`](docs/performance.md) for the memory budget and
benchmarks.

## Hardware

| Part | Details |
|---|---|
| MCU | Raspberry Pi Pico 2 W (RP2350, 4 MB flash, 520 KB SRAM) |
| Display | 4.0" TFT SPI 480×320 V1.0, ILI9488, 14-pin header (`KMRTM40045-SPI`) |
| Touch | HR2046 (XPT2046-compatible) resistive, separate SPI bus |
| Storage | Internal LittleFS (2 MB partition) |
| Backlight | GP21 → module `LED` control input (onboard driver), PWM |

Full pin map, power notes and the ILI9488 MISO warning: [`docs/wiring.md`](docs/wiring.md).

## Build and flash (USB)

```bash
# One-time setup: install PlatformIO Core
pip install platformio

# Build
pio run -e pico2w_usb

# Flash (board in BOOTSEL mode or with USB serial available)
pio run -e pico2w_usb -t upload

# Serial monitor
pio device monitor -b 115200
```

The firmware prints a banner with the version, board, display size, LittleFS statistics and free heap.

### Display diagnostics

The normal `pico2w_usb` build boots to the splash screen and then the paint UI.
The `pico2w_diag` build runs the SPI benchmarks before the UI and also logs UI
flush times:

```bash
pio run -e pico2w_diag -t upload
pio device monitor -b 115200
```

Look for the `[bench]` lines, `[disp] splash flush`, and `[ui] flush` timings.
Record them in [`docs/performance.md`](docs/performance.md). The display runs at
40 MHz, which was verified stable on the panel.

## Touch bring-up

The HR2046 touch controller uses a separate SPI bus from the display:

| Signal | Pico GPIO | Module pin |
|---|---|---|
| Touch SCK | GP10 | T_CLK |
| Touch MOSI | GP11 | T_DIN |
| Touch MISO | GP12 | T_OUT |
| Touch CS | GP13 | T_CS |
| Touch IRQ | GP14 | T_IRQ (optional; polling is used) |

Do **not** connect the display's SDO/MISO to the touch bus. The normal build
prints debounced gesture events while the UI is live:

```text
[touch] down x=... y=...
[touch] up x=... y=... duration=... ms tap=1
[touch] tap x=... y=...
[touch] long-press x=... y=...
```

The `pico2w_diag` build additionally prints `[touch] move` and raw
`[touch] raw=... z=... mapped=... pressed=...` samples. There is no touch
crosshair in the normal build; use the UI and the serial events for bring-up.

On the device, open **Menu → Settings → Calibrate** and touch the four
crosshairs in this order: top-left, top-right, bottom-left, bottom-right. The
firmware fits a full affine transform (the raw axes are rotated 90° relative to
the display, so cross terms are included), checks it against the four samples
and saves it to `/config.bin`. On boot the saved transform is loaded and
applied; without a valid saved file the factory map is used. The factory map
comes from full-corner samples and maps the corners to `(0,0)`, `(319,0)`,
`(0,479)` and `(319,479)`.

## OTA updates

OTA uses arduino-pico's `ArduinoOTA` plus PlatformIO's `espota` upload protocol — the same workflow as an ESP32, with no web page.

1. Flash once over USB with an OTA-enabled build.
2. Copy `include/secrets.example.h` to `include/secrets.h` and set the Wi-Fi credentials and an OTA password.
3. Reboot and confirm **Menu → Wi-Fi & OTA** shows an IP address, `OTA server: listening` and `OTA reserve: OK`.
4. Upload over the LAN:

```bash
export PAINT_OTA_IP=192.168.1.42
export PAINT_OTA_PASSWORD='your-ota-password'
pio run -e pico2w_ota -t upload
```

**WSL2 users:** build with `pio run -e pico2w_ota`, then upload with
`tools/ota_windows.sh`. ArduinoOTA needs the Pico to open a TCP connection back
to the uploader, and a WSL2 listener is not reachable at the Windows LAN
address; the plain `-t upload` command authenticates and then stalls at 0%.
The helper runs Windows Python's espota against the same build output.

The device shows the firmware-update overlay and reboots when the staged image
is complete. The gallery and `/config.bin` are preserved because only the
sketch partition is updated. If Wi-Fi is unavailable, the app continues to run
offline; updates can always be recovered over USB/BOOTSEL.

See [`docs/ota.md`](docs/ota.md) for configuration, static IPs, safety rules
and troubleshooting.

## Project layout

```
include/
  config.h              pins, dimensions, limits
  version.h             firmware version and build stamp
  secrets.example.h     Wi-Fi/OTA credentials template
  paint/                canvas, brush, stroke, colour/HSV, undo, geometry, tools,
                        CRC32, RLE codec, .pnt format
  display/              ILI9488 driver and graphics primitives
  input/                touch abstraction and calibration
  storage/              LittleFS, settings, .pnt save/load, gallery
  ui/                   screens, widgets, icons, font
  net/                  Wi-Fi and ArduinoOTA lifecycle
src/
  main.cpp              boot, filesystem, UI loop, OTA callbacks, heartbeat
  display/              ILI9488 driver and graphics primitives
  net/                  non-blocking Wi-Fi/OTA state machine
  storage/              settings persistence (/config.bin), .pnt atomic saves
  ui/                   immediate-mode UI, sheets/HSV picker, 5×7 bitmap font
test/
  test_native/          host unit tests (Unity)
docs/
  wiring.md             confirmed pin map and power notes
  performance.md        build footprint and display benchmarks
  ota.md                OTA operations
  user-guide.md         quick start, tools, settings, idle and recovery
  mockups/              SVG UI mockups used by the user guide
```

## Native tests

```bash
pio test -e native
```

The native environment builds the Arduino-free UI, graphics, font and storage
codec sources alongside the tests, so it exercises the real canvas/brush/undo
code plus the UI flows (New confirmation, HSV picker, undo/redo requests, the
gallery/storage screens, the Network-screen toggle, the M8 Settings controls,
safe-mode self-tests and accessibility targets) and the `.pnt` codec/format.
Pure logic (colour conversion, geometry, CRC/RLE, `.pnt`, calibration, undo
bookkeeping, orientation mapping, idle thresholds and boot-health encoding)
has no Arduino dependency and runs on the host. LittleFS-backed save/load and
the Wi-Fi/OTA failure paths are exercised on hardware. The suite currently
passes **75/75**.

> Native tests need a host C++ toolchain (`gcc`/`g++`). On Windows, run them from WSL/Linux or install MinGW; the CI workflow runs them on Ubuntu.

## Notes

- The official `platformio/raspberrypi` platform only ships RP2040 boards. This project uses the RP2350-capable [maxgerhardt fork](https://github.com/maxgerhardt/platform-raspberrypi), pinned to a commit in `platformio.ini`.
- The ILI9488 SPI interface only accepts 18-bit colour, so the firmware keeps an RGB565 framebuffer and expands to RGB666 during transfer.
- Display MISO is deliberately not connected; touch uses a second SPI bus.
- Undo history is a fixed ~64 KB in-object pool (44 RLE tiles + 7 raw tiles).
  It is compressed per stroke and evicts the oldest action when full; **New
  canvas** clears it.
- Do not fill LittleFS with gallery images: OTA stages the new firmware there. The app keeps a free-space reserve.
