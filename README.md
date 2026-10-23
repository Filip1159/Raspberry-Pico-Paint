# Pico Paint

A small paint application for the **Raspberry Pi Pico 2 W** with a **4" ILI9488 480×320 SPI TFT** and an **HR2046 / XPT2046-compatible resistive touch panel**.

The app runs in **portrait (320×480)** and is designed to work fully offline. Wi-Fi is only used for OTA updates.

## Status

Milestone **M1** is hardware-verified (serial banner, 2 MB LittleFS, free heap).
Milestone **M2** — ILI9488 driver, RGB565→RGB666 flush, graphics primitives and
the diagnostics screen — is hardware-verified: correct colours, upright portrait
(rotation 2), stable 40 MHz SPI and a 139.3 ms full-screen flush. Milestone **M3**
(touch input and UI shell) is in progress: the XPT2046/HR2046 driver is
hardware-validated, and a 4-point affine calibration, touch state machine and
temporary touch crosshair are implemented. The UI shell is next. The paint
engine and UI land in M3–M6; OTA lands in M7. See
[`BACKLOG.md`](BACKLOG.md) for the full plan and
[`docs/performance.md`](docs/performance.md) for benchmark results.

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

The normal `pico2w_usb` build currently shows the M2 diagnostic screen: title
text, colour bars, grey ramp, checkerboard, a circle and a line. The `pico2w_diag`
build additionally runs the SPI benchmarks:

```bash
pio run -e pico2w_diag -t upload
pio device monitor -b 115200
```

Look for the `[bench]` lines and the `[disp] diagnostics flush` timing. Record
them in [`docs/performance.md`](docs/performance.md). The initial SPI clock is a
conservative 27 MHz; it will be raised after the panel is proven.

## Touch bring-up

The HR2046 touch controller uses a separate SPI bus from the display:

| Signal | Pico GPIO | Module pin |
|---|---|---|
| Touch SCK | GP10 | T_CLK |
| Touch MOSI | GP11 | T_DIN |
| Touch MISO | GP12 | T_OUT |
| Touch CS | GP13 | T_CS |
| Touch IRQ | GP14 | T_IRQ (optional; polling is used) |

Do **not** connect the display's SDO/MISO to the touch bus. The current build
prints raw samples over serial:

```text
[touch] raw=... z=... mapped=... pressed=0
```

When the panel is pressed it prints more often and draws a red crosshair at the
mapped screen position. The gesture layer emits debounced events:

```text
[touch] down x=... y=...
[touch] move x=... y=...
[touch] up x=... y=... duration=... ms tap=1
[touch] tap x=... y=...
[touch] long-press x=... y=...
```

The calibration uses four corner samples in this order: top-left, top-right,
bottom-left, bottom-right. The raw touch axes are rotated 90° relative to the
display, so the affine transform includes cross terms. Expected mapped corners
are `(0,0)`, `(319,0)`, `(0,479)` and `(319,479)`.

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
  paint/                canvas, colour, geometry, tools, undo, codec
  display/              ILI9488 driver and graphics primitives
  input/                touch abstraction and calibration
  storage/              LittleFS, settings, gallery
  ui/                   screens, widgets, icons, font
  net/                  Wi-Fi and ArduinoOTA lifecycle
src/
  main.cpp              boot, filesystem, diagnostics, heartbeat
  display/              ILI9488 driver and graphics primitives
  ui/                   5×7 bitmap font
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

Pure logic (colour conversion, geometry, codec, calibration, undo bookkeeping) is kept Arduino-free so it can run on the host.

> Native tests need a host C++ toolchain (`gcc`/`g++`). On Windows, run them from WSL/Linux or install MinGW; the CI workflow runs them on Ubuntu.

## Notes

- The official `platformio/raspberrypi` platform only ships RP2040 boards. This project uses the RP2350-capable [maxgerhardt fork](https://github.com/maxgerhardt/platform-raspberrypi), pinned to a commit in `platformio.ini`.
- The ILI9488 SPI interface only accepts 18-bit colour, so the firmware keeps an RGB565 framebuffer and expands to RGB666 during transfer.
- Display MISO is deliberately not connected; touch uses a second SPI bus.
- Do not fill LittleFS with gallery images: OTA stages the new firmware there. The app keeps a free-space reserve.
