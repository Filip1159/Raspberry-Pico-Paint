# Pico Paint

A small paint application for the **Raspberry Pi Pico 2 W** with a **4" ILI9488 480×320 SPI TFT** and an **HR2046 / XPT2046-compatible resistive touch panel**.

The app runs in **portrait (320×480)** and is designed to work fully offline. Wi-Fi is only used for OTA updates.

## Status

Milestones **M1–M3** are hardware-verified: serial banner and 2 MB LittleFS;
the ILI9488 driver with RGB565→RGB666 flush, correct colours, upright portrait
(rotation 2), stable 40 MHz SPI and a 139.3 ms full-screen flush; and the
XPT2046/HR2046 touch stack with 4-point affine calibration, debounced gestures,
an 800 ms splash, canvas, 48 px top bar, bottom sheets, toasts and placeholder
Gallery/Settings/About screens. Settings → **Calibrate touch** saves its
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

M4 firmware builds are clean (`pico2w_usb` RAM 138,032 B / flash 419,576 B;
`pico2w_ota` RAM 138,032 B / flash 419,576 B; `pico2w_diag` RAM 138,132 B /
flash 420,864 B), and the native suite passes **40/40**. Hardware profiling
showed a flat **57,008 B** free heap for 5+ minutes, first-dab flushes of
0.33–1.0 ms, and 1/8/24 px stroke flushes of ~0.14/0.15/0.66 ms. Long 8/24 px
squiggles can exceed the 32-tile undo cap, and HSV taps take 90–103 ms; these
are tracked as follow-ups M4-11/M4-12. M5–M6 add the shape, fill and
eyedropper tools plus storage/gallery; OTA lands in M7. See
[`BACKLOG.md`](BACKLOG.md) for the full plan and
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

On the device, open **Menu → Settings → Calibrate touch** and touch the four
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
3. Upload over the LAN:

```bash
export PAINT_OTA_IP=192.168.1.42
export PAINT_OTA_PASSWORD='your-ota-password'
pio run -e pico2w_ota -t upload
```

See [`docs/ota.md`](docs/ota.md) for details, safety rules and troubleshooting.

## Project layout

```
include/
  config.h              pins, dimensions, limits
  version.h             firmware version and build stamp
  secrets.example.h     Wi-Fi/OTA credentials template
  paint/                canvas, brush, stroke, colour/HSV, undo, geometry, tools
  display/              ILI9488 driver and graphics primitives
  input/                touch abstraction and calibration
  storage/              LittleFS, settings, gallery
  ui/                   screens, widgets, icons, font
  net/                  Wi-Fi and ArduinoOTA lifecycle
src/
  main.cpp              boot, filesystem, UI loop, heartbeat
  display/              ILI9488 driver and graphics primitives
  storage/              settings persistence (/config.bin)
  ui/                   immediate-mode UI, sheets/HSV picker, 5×7 bitmap font
test/
  test_native/          host unit tests (Unity)
docs/
  wiring.md             confirmed pin map and power notes
  performance.md        build footprint and display benchmarks
  ota.md                OTA operations
```

## Native tests

```bash
pio test -e native
```

The native environment builds the Arduino-free UI, graphics and font sources
alongside the tests, so it exercises the real canvas/brush/undo code plus the
UI flow (New confirmation, HSV picker, undo/redo requests). Pure logic (colour
conversion, geometry, codec, calibration, undo bookkeeping) has no Arduino
dependency and runs on the host.

> Native tests need a host C++ toolchain (`gcc`/`g++`). On Windows, run them from WSL/Linux or install MinGW; the CI workflow runs them on Ubuntu.

## Notes

- The official `platformio/raspberrypi` platform only ships RP2040 boards. This project uses the RP2350-capable [maxgerhardt fork](https://github.com/maxgerhardt/platform-raspberrypi), pinned to a commit in `platformio.ini`.
- The ILI9488 SPI interface only accepts 18-bit colour, so the firmware keeps an RGB565 framebuffer and expands to RGB666 during transfer.
- Display MISO is deliberately not connected; touch uses a second SPI bus.
- Undo history is a fixed ~64 KB in-object pool (44 RLE tiles + 7 raw tiles).
  It is compressed per stroke and evicts the oldest action when full; **New
  canvas** clears it.
- Do not fill LittleFS with gallery images: OTA stages the new firmware there. The app keeps a free-space reserve.
