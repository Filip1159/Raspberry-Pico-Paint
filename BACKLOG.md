# Pico Paint — Backlog & Implementation Plan

**Target hardware:** Raspberry Pi Pico 2 W (RP2350, 4 MB flash, 520 KB SRAM) + 4" 480×320 ILI9488 SPI TFT (used in **portrait 320×480**) with **HR2046 / XPT2046-compatible** resistive touch
**Status:** M0–M6 hardware-validated; M7-1–M7-8 (non-blocking Wi-Fi, Wi-Fi & OTA status screen, ArduinoOTA callbacks, OTA autosave/undo handling, progress overlay, reserve preflight, `pico2w_ota` environment and version banner) implemented and native-tested; M7-12 security documentation complete; a first OTA upload is confirmed through the Windows helper; M7-9–M7-11 hardware validation remains. **M8-1–M8-9 are source-complete and native-tested, but M8 hardware acceptance is still pending** (rotation/touch mapping, idle wake, persistence, recovery entry, boot-health counting and self-tests). The M6 save/load/gallery, autosave recovery, storage gauge, config migration and diagnostics flows were confirmed on hardware; M4/M5 follow-up re-measurement and the long-term flash-wear soak remain.
**Last updated:** 2026-10-26

---

## 1. Product goal

A self-contained, finger-friendly drawing tablet:

- Boots straight into a canvas in under ~2.5 s.
- Smooth freehand drawing with a visible, low-latency stroke.
- A small but complete tool set: brush, eraser, line, rectangle, ellipse, flood fill, eyedropper.
- 16-bit colour, multiple brush sizes, an on-screen palette + HSV picker.
- Undo/redo, save/load to internal flash, a thumbnail gallery, and crash-safe autosave.
- Wi-Fi OTA updates launched from PlatformIO (`pio run -e pico2w_ota -t upload`), no web page required.
- Works fully offline; Wi-Fi is optional and never blocks painting.

### Scope line

| Stage | Includes | Outcome |
|---|---|---|
| **MVP** | M0–M6 | A genuinely usable offline paint app with save/load |
| **v1.0** | MVP + M7–M8 | OTA, settings, polish, docs |
| **Release** | v1.0 + M9 | Hardening, tests, CI, tagged artifacts |

### Explicitly out of scope for v1

Layers, zoom/pan, text tool, image import, SD card, cloud sync, PNG export over Wi-Fi, pressure-sensitive stylus (resistive panel only reports pressure crudely), battery management, enclosure design.

---

## 2. Current repository state

- PlatformIO project retargeted to **Pico 2 W** (`board = rpipico2w`) via the RP2350-capable maxgerhardt platform fork, pinned to a commit.
- M1 skeleton is in place and **hardware-verified on 2026-10-24**: serial banner, LittleFS `total=2097152 used=8192 free=2088960`, free heap 452,472 B. The onboard backlight driver is confirmed: `LED` tied to 3V3 through 10k lights the panel.
- M2 implementation is in place: ILI9488 init/rotation/address-window driver, RGB565→RGB666 line-buffer flush, `gfx` primitives (rect, hline/vline, line, circle, fillCircle, text, blit), 5×7 font, dirty-region helper, benchmarks and the original diagnostics screen (now retired from the normal boot).
- M4 builds verified on 2026-10-24:
  - `pico2w_usb`: **SUCCESS** — RAM 138,032 B (26.3%), flash 419,576 B (20.0%)
  - `pico2w_ota`: **SUCCESS** — RAM 138,032 B, flash 419,576 B
  - `pico2w_diag`: **SUCCESS** — RAM 138,132 B (26.3%), flash 420,864 B (20.1%)
- Native tests pass under WSL g++ (**40/40**), including canvas/brush/stroke, compressed undo/redo and raw-tile fallback, HSV conversion, UI layout, New-canvas confirmation, HSV-picker flow, HSV dirty bounds and undo/redo requests. `pio test -e native` now builds the Arduino-free UI/gfx/font sources; run it in WSL/Linux or CI because the Windows host has no `gcc`/`g++`.
- M4 was **hardware-validated on 2026-10-24** with the diagnostics build on COM16. Drawing, undo, New-canvas clear and the overlay flows work. Free heap is **57,008 B** on the normal build and **56,904 B** on the diag build, flat through 5+ minutes of activity; the estimated 58,576 B budget differs only by allocator overhead.
- M4 flushes: 1 px stroke ~139–152 µs/frame (100 Hz touch-limited), 8 px ~143–153 µs (87–96 Hz), 24 px ~648–681 µs (78–93 Hz); first-dab flush 0.33–1.0 ms. A 3.9 s 1 px stroke used 31 undo tile records / 4,560 B; five 24 px dots used 11 records / 1,372 B and undid in 2.1–3.1 ms.
- M4 profiling found two follow-ups: long 8/24 px squiggles exceeded the 32-tile per-action undo cap and showed "Stroke too large to undo" (M4-11), and HSV taps take 90–103 ms with the 179 ms HSV open, above the 50 ms target (M4-12).
- M4-11 is implemented: one action may now reference all **51** tile records (up from 32), and a 40-tile action plus the 52-tile overflow path are covered by native tests. M4-12 is implemented: the HSV square paints a 32×32 grid, S/V taps dirty only the old/new 21×21 marker bounds, and hue taps dirty only 7×30 marker bounds plus the square. Hardware re-measurement of both is still pending.
- M5 builds verified on 2026-10-24; latest build after the follow-ups: `pico2w_usb` RAM **160,208 B (30.6%)**, flash **430,212 B (20.6%)**; `pico2w_diag` RAM **160,308 B**, flash **431,492 B (20.6%)**. Native suite **47/47** including shape preview/cancel, commit+undo for rect/ellipse/filled shapes, 40-tile undo actions, flood fill inside/outside outline, flood fill noop, tool options and canvas clamping. Free heap budget **36,400 B**.
- M6-1–M6-3 are implemented on 2026-10-25: `paint/crc32.h` (IEEE CRC-32), `paint/codec_rle` (16-bit run/literal tokens, strict decoder and validator), `paint/pnt` (32-byte header, streaming RLE + raw 64×86 thumbnail, verified decode) and `storage/pnt_file` (`.tmp` write, CRC/RLE verification, then rename). The native suite covers known CRC vectors, incremental CRC, RLE round-trip/worst-case/corrupt streams, header validation, pixel-identical `.pnt` round-trip, thumbnail sampling, CRC rejection and bad-argument paths.
- M6-4/M6-5 are implemented on 2026-10-25: the menu has separate **Save**/**Save as** entries; Save overwrites the current document or asks for a new name, and Save as allocates the next free `imgNNNN.pnt` and confirms it. Gallery taps open a load confirmation that warns about unsaved changes; long-press opens a delete confirmation. The gallery is a paged 2×2 grid with streamed thumbnails, filenames, page arrows and an empty state. `storage/paint_files` scans/sorts up to 32 `*.pnt` files and allocates names by checking the real directory, so Save as cannot collide with files beyond the index cap. At that stage the native suite was **63/63** (name/path helpers, menu layout, save-as flow, gallery paging, load/delete confirmations and empty state), with builds at `pico2w_usb`/`pico2w_ota` RAM **161,352 B**, flash **439,300 B** and `pico2w_diag` RAM **161,452 B**, flash **440,588 B**.
- M6-6 is implemented on 2026-10-25: `paint::AutosaveTimer` arms on the first change and saves at most 15 s later; further changes do not postpone the deadline. Autosaves go to `/paint/autosave.pnt` through the existing atomic `savePnt()` path and are deferred while a brush or shape preview is active. The gallery excludes the autosave. At boot `isPntFileValid()` checks the header, exact dimensions, size, CRC32 and RLE structure; a valid file shows a modal Recovery screen with Restore/Discard, and an invalid one is removed. Restore loads the pixels as an unnamed dirty document and keeps the autosave until the next clean save; Discard deletes it. Successful Save/Save as, load and New canvas also delete it, while failed saves/recovery keep it. At the M6-6 stage the native suite was **65/65** (autosave schedule, autosave naming and boot recovery flows), with builds at `pico2w_usb`/`pico2w_ota` RAM **161,360 B**, flash **441,252 B** and `pico2w_diag` RAM **161,460 B**, flash **442,516 B**. Recovery was exercised on hardware in M6-10; long-term flash wear remains a follow-up.
- M6-7 is implemented on 2026-10-25: `storage::StorageStats` computes used/free bytes and the 700 KB OTA reserve, and `Storage` now owns the LittleFS mount/stats/format helpers. Settings shows a storage gauge with free KB, used percentage and an `OTA reserve: OK/LOW` status. The Wi-Fi & OTA menu entry refuses to continue when free space is below **716,800 B** and reports `Need 700 KB free for OTA`; saves below the reserve still succeed but report `Saved … - OTA low` so drawings can still be rescued. At the M6-7 stage the native suite was **66/66** (space/headroom calculations and the UI guard), with builds at `pico2w_usb`/`pico2w_ota` RAM **161,376 B**, flash **442,196 B** and `pico2w_diag` RAM **161,476 B**, flash **443,468 B**. The gauge was confirmed on hardware in M6-10; the live OTA refusal still needs the M7 hardware run.
- M6-8/M6-9 are implemented on 2026-10-25: `/config.bin` moved to a version 2, 44-byte payload with rotation, brightness, OTA enable, flags, the six calibration values and a 16-byte last-opened filename. Version 1 (28-byte) blobs still decode; the new fields default to empty/zero and the file is upgraded on the next save. Save/Save as and load record the last-opened name; New, deleting that file and recovery Restore/Discard clear it. Settings → **Storage details** adds a diagnostics page with mount status, total/used/free KB, the real `PaintFileIndex::totalCount()` (files beyond the 32-entry gallery index are counted) and the OTA-reserve state. The native suite is now **69/69** (adds v2 round-trip, v1 migration, invalid/autosave last-opened rejection and the Storage screen flow). Current builds: `pico2w_usb`/`pico2w_ota` RAM **161,404 B**, flash **443,380 B**; `pico2w_diag` RAM **161,504 B**, flash **444,652 B**; free-heap estimate **35,204 B**. Config migration and the diagnostics page were confirmed on hardware in M6-10.
- M4 drawing engine is implemented and hardware-validated: clipped `Canvas` over the shared framebuffer, seven round brush masks, gap-free brush/eraser strokes with per-dab undo capture, palette + five recent colours, HSV picker, compressed undo/redo, New-canvas confirmation, and overlay-safe presentation for sheets/toasts/screens. Host-preview screenshots exercise the art, sheets, toast, undo/redo, New confirmation, HSV picking and recent-colour row.
- Undo/redo uses a fixed ~64 KB in-object pool: 44 RLE tile records + 7 raw tile records, up to 16 actions and one action may use all 51 tile records. Each action captures "before" tiles, converts them to XOR deltas at commit, and evicts the oldest action when the pool fills. Ten normal undo levels fit in cases that previously exhausted the 30 raw-tile pool; pathological strokes can still trim history, but long multi-second squiggles no longer fail at the old 32-tile cap.
- M2 is hardware-verified: correct colours, upright portrait at rotation 2, no offset/clipping, stable 40 MHz SPI, 139.3 ms full-screen flush, 143,360 B free heap after the framebuffer.
- M3-1 is hardware-validated: the HR2046 responds on SPI1 at all four corners. Raw ranges are roughly X 260–3880 and Y 298–3807. The normal build prints `down`, `up`, `tap` and `long-press`; `pico2w_diag` additionally prints `move`, raw samples and UI flush timings.
- M3-2 calibration is implemented as a full Q16.16 affine least-squares fit from four corner samples. The panel's raw axes are rotated 90° relative to the display (raw X top→bottom, raw Y left→right), so the transform includes cross terms; all four corners map within ~1 px. Settings are stored in `/config.bin` with a magic/version/FNV-1a checksum header and atomic `/config.tmp` → rename replacement; implausible or corrupt calibration falls back to the factory map.
- M3-3 touch state machine is implemented: two-sample press/release debounce, 2 px move threshold, tap detection (≤10 px travel), one-shot 600 ms long-press, and a frame structure for the UI. The diagnostics build prints `down`, `move`, `up`, `tap` and `long-press` events.
- M3-4/M3-5/M3-6 UI shell is implemented: `Ui` layout and hit testing, 48 px top bar with six 44×44 buttons, blank paper canvas, vector icons, tools/palette/size/menu bottom sheets, toast, and Gallery/Settings/About/Recovery placeholders. The 5×7 font now indexes the full 256-glyph table from ASCII 0, which fixed the garbled host-preview text.
- M3-8/M3-9 guided calibration is implemented and hardware-verified on 2026-10-24: Settings → **Calibrate touch** shows four inset crosshairs (top-left, top-right, bottom-left, bottom-right), captures the raw sample on each debounced press, validates the affine fit against all four targets, applies it immediately and saves it to `/config.bin`. The first run saved coefficients `a=-74 b=-5649 c=341 d=8463 e=-41 f=-26`; after reboot the firmware logged `[cfg] loaded /config.bin` and `[cfg] touch calibration applied`. Rotation remains locked to 2.
- The UI has been rendered on the host at 320×480 for splash, canvas, every sheet, placeholders and toast, and is now hardware-verified on 2026-10-24: splash → canvas, all six top-bar buttons, tools/palette/size/menu sheets, toasts, and Gallery/Settings/About back navigation. Free heap is **142,688 B** on the calibration build (142,760 B on the later reboot build).
- Measured calibration-flow flushes: Settings **Calibrate touch** button (280×44) 11.2 ms, "Calibration saved" toast (288×36) 9.5 ms, and each full-screen crosshair step ≈139.4 ms. The earlier sheet-button optimization is confirmed: opening the menu flushed only `258,2 44×44` (1.87 ms) plus the sheet, not the whole top bar.
- Measured M3 UI flushes: 44×44 top-bar button 1.85–1.98 ms; 288×36 toast 9.44 ms; sheet flushes 34.9–98.7 ms; full-screen transition 139.3 ms. `setSheet()` now dirties only the affected 44×44 button(s) instead of the whole 320×48 bar, saving ~12 ms per sheet transition. Details in `docs/performance.md`.
- Git repository initialized (`main`); M1, M2, the M3 touch driver/UI shell and the M4 drawing engine are committed.

**Next action:** Run the M8 hardware acceptance and then finish M7-9–M7-11. First flash `pico2w_usb` and verify rotation 0/2 (a true 180° flip, not a mirror), touch mapping and calibration in both rotations, all four brightness steps, 60 s dim / 5 min off / first-touch wake, settings persistence across a power cycle, safe-mode entry by hold-at-boot, the three boot-health failures, and the Display/Touch/Storage self-tests. Then power the Pico, confirm its current IP in the Wi-Fi & OTA screen, and run ten consecutive `tools/ota_windows.sh` uploads while checking that drawings/config survive. Exercise wrong-password, Wi-Fi loss, power loss and low-space paths, and verify USB/BOOTSEL recovery. Record `[mem] free heap before net` / `[mem] free heap` from the serial log and re-measure the M4-11 long strokes, M4-12 HSV taps, the filled-shape options row and the M5-8 clamp/cancel edge cases on the same hardware session. The pre-change measurements and expected payload reductions are in `docs/performance.md`.

---

## 3. Locked decisions

| Area | Decision | Why |
|---|---|---|
| MCU board | Raspberry Pi Pico 2 W (`board = rpipico2w`) | Requested hardware; RP2350 has enough RAM for a full framebuffer |
| Display | ILI9488, 480×320 native, 4-wire SPI | Requested hardware |
| Display driver | **Custom lightweight driver** (no TFT_eSPI/LVGL) | TFT_eSPI's RP2350 + ILI9488 support is unreliable; we only need a small feature set |
| Colour | RGB565 framebuffer, expanded to RGB666 on transfer | ILI9488 SPI only accepts 18-bit colour; RGB565 halves RAM use |
| Touch | **HR2046 (XPT2046-compatible) resistive touch on a separate SPI bus** — confirmed from module photo | Separate bus avoids the ILI9488 MISO problem; the HR2046 uses the standard XPT2046 command set |
| UI | Custom immediate-mode UI + dirty rectangles | Requested; smallest, fastest, no dependency |
| Storage | Internal LittleFS, 2 MB partition | Requested; OTA staging also needs filesystem space |
| Save format | Custom RLE-compressed RGB565 `.pnt` + thumbnail | Fast, simple, no codec dependencies |
| OTA | Arduino-pico `ArduinoOTA` + PlatformIO `upload_protocol = espota` | Console-driven OTA, exactly like ESP32; no browser needed |
| Orientation | **Portrait 320×480** (native panel rotated), rotation still configurable | Confirmed by user; natural sketchpad shape |
| Language | English UI; strings centralised for later Polish translation | User communicates in English; easy to localise |

---

## 4. Hardware

### 4.1 Display module — confirmed from photo

The module is a **4.0" TFT SPI 480×320 V1.0** board (silkscreen `KMRTM40045-SPI`, FCC mark) with a 14-pin 2.54 mm header. Header labels, top to bottom:

`T_IRQ, T_OUT, T_DIN, T_CS, T_CLK, SDO(MISO), LED, SCK, SDI, D/C, RESET, CS, GND, VCC`

`T_OUT` is the touch controller's data-out (MISO).

The touch controller is **U2 = HR2046**, a 16-pin XPT2046-compatible resistive touch controller. It uses the standard XPT2046 command/register set, so an XPT2046 driver works unchanged. The board also has a microSD slot (unused in v1).

There is **no capacitive-touch variant** on this board. The display and touch share the module PCB but expose separate SPI pins, which lets us use two independent SPI buses.

> **ILI9488 MISO warning:** the ILI9488 SDO/MISO pin does not tri-state reliably when CS is high. Never share the display's MISO with another SPI device. This plan puts touch on a **separate SPI bus**, so display MISO stays unconnected.

### 4.2 Proposed pin map (Pico 2 W)

| Function | Pico GPIO | Module pin | Notes |
|---|---|---|---|
| Display VCC | 3V3 | VCC | 3.3 V logic; verify module regulator |
| Display GND | GND | GND | Common ground, short return |
| Display CS | GP17 | CS | SPI0 hardware CS |
| Display DC | GP16 | D/C | Command/data |
| Display RST | GP20 | RESET | Can share board reset; keep dedicated |
| Display MOSI | GP19 | SDI | SPI0 TX |
| Display SCK | GP18 | SCK | SPI0 SCK |
| Display MISO | — | SDO(MISO) | **Not connected** |
| Backlight | GP21 | LED | High-level control input; module has onboard backlight transistor. Verify before assuming; fallback MOSFET if the pin is raw LED power |
| Touch SCK | GP10 | T_CLK | SPI1 SCK |
| Touch MOSI | GP11 | T_DIN | SPI1 TX |
| Touch MISO | GP12 | T_OUT | SPI1 RX; touch data out |
| Touch CS | GP13 | T_CS | SPI1 CS |
| Touch IRQ | GP14 | T_IRQ | Optional; polling is acceptable |
| SD card | — | SD pins | Unused in v1 |

**Reserved / do not use:** GP23, GP24, GP25, GP29 (CYW43 Wi-Fi). GP26–GP28 are ADC pins — free, but keep them for analog inputs/buttons.
**Spare:** GP0–GP9, GP15, GP22 — reserve GP15/GP22 for a physical button or a future capacitive-touch upgrade.

### 4.3 Wiring rules

- Keep SPI wires short (<10 cm) and provide a solid ground return; 40 MHz is unforgiving of long jumpers.
- Add 100 µF bulk + 100 nF decoupling near the display connector.
- Run the panel at 3.3 V. If the module has a 5 V regulator, still use 3.3 V logic.
- Backlight: the module appears to have an onboard backlight transistor, so `LED` is a high-level control input. Drive it directly from GP21 (1k series, 10k pull-down) and PWM at 1–2 kHz. Verify with a current measurement: a few mA on `LED` means control input; ~100 mA means raw LED power. Only in the raw case use a logic-level MOSFET (not a BC547, which is limited to 100 mA).
- Power budget: display logic + backlight can draw 100–200 mA; Wi-Fi peaks add ~200 mA. Power from a good USB source or a dedicated 5 V supply, not from another board's 3V3 pin.

### 4.4 Hardware questions

Resolved:

- [x] **Touch controller:** HR2046 (XPT2046-compatible), confirmed from the module photo.
- [x] **Interface:** 14-pin 4-wire SPI, 480×320, confirmed from silkscreen.
- [x] **Orientation:** portrait 320×480.

Still to verify on the bench:

- [ ] `LED` pin current (control input vs raw LED) and total backlight current at 3.3 V.
- [ ] Is 4 MB flash correct for this Pico 2 W? (Standard Pico 2 W = 4 MB.)
- [ ] Will there be a physical button for reset/BOOTSEL access in the enclosure?
- [ ] Is the touch panel's raw range usable across the whole screen (check calibration at all four corners)?

---

## 5. Software architecture

### 5.1 Layers

```
main.cpp
 └─ App            screen/state machine: splash → canvas → gallery → settings → OTA
     ├─ UI         immediate-mode widgets, layout, icons, fonts, dirty regions
     ├─ Input      touch driver → calibration → filter → gesture/stroke events
     ├─ Paint      canvas, brush, tools, undo/redo, document model
     ├─ Storage    LittleFS, .pnt RLE codec, config, autosave, gallery
     ├─ Net        non-blocking Wi-Fi, ArduinoOTA, version/status
     └─ HAL        ILI9488 SPI display, HR2046/XPT2046 touch, backlight, time
```

Rule: `Paint`, `Storage` codecs, and geometry helpers must be **Arduino-free** so they can be unit-tested on the host (`native` env).

### 5.2 Module map

| Path | Responsibility |
|---|---|
| `include/config.h` | Pins, dimensions, limits, compile-time flags |
| `include/version.h` | `PAINT_VERSION`, build date/time |
| `include/paint/color.h` | RGB565/666/888 conversions, HSV |
| `include/paint/geometry.h` | Point, Rect, clipping, line/circle rasterisation |
| `include/paint/canvas.h` | Framebuffer, pixel ops, dirty-rect accumulation |
| `include/paint/brush.h` | Precomputed round brush masks, stroke interpolation |
| `include/paint/tools.h` | Brush, eraser, line, rect, ellipse, fill, picker |
| `include/paint/undo.h` | Tile snapshot ring + large-action RLE snapshots |
| `include/paint/document.h` | Metadata, save/load orchestration |
| `include/paint/codec_rle.h` | RLE encode/decode + CRC32 |
| `include/storage/storage.h` | LittleFS mount, files, free space, atomic writes |
| `include/storage/settings.h`, `settings_codec.h` | Rotation, brightness, OTA flag, calibration, last-opened filename |
| `include/storage/autosave.h` | 15 s autosave schedule |
| `include/storage/paint_files.h` | Gallery scan/sort and next-name allocation |
| `include/storage/pnt_file.h` | Atomic `.pnt` save/load/validate |
| `include/storage/boot_health.h` | 8-byte boot-health blob, CRC and threshold (header-only codec) |
| `include/ui/ui.h` | Screens, widgets, hit testing, toasts, modals, Settings/recovery |
| `include/ui/idle.h` | Idle dim/off/wake state machine |
| `include/ui/font5x7.h` | 5×7 font table and glyph range |
| `include/ui/icons.h` | Reserved 1-bit icon structure for the M8 icon set |
| `include/input/touch.h` | `ITouch` interface + `Xpt2046Touch` (works with HR2046) |
| `include/input/calibration.h` | 4-point affine calibration |
| `include/input/orientation.h` | Canonical rotation-2 ↔ display 0/2 mapping |
| `include/display/ili9488.h` | SPI init, window, RGB666 expansion, flush |
| `include/display/gfx.h` | Lines, rects, circles, text, clipping |
| `include/net/net.h` | Wi-Fi state machine, ArduinoOTA lifecycle |
| `src/main.cpp` | Setup + cooperative main loop |

### 5.3 Memory budget (target, RP2350 has 520 KB SRAM)

| Item | Bytes | Notes |
|---|---:|---|
| 320×480 RGB565 framebuffer | 307,200 | Full screen, including UI |
| Undo pool (44 RLE + 7 raw records) | 65,536 | One action may use all 51 records; see §5.5 |
| Display line buffer (480×3 B) | 1,440 | 480 px maximum width; single buffer until DMA |
| UI state, fonts, icons | ≈ 8,000 | Flash-resident assets |
| Wi-Fi + lwIP buffers | ≈ 50,000 | Measure in M7; may vary |
| Stacks (2 cores) + heap headroom | ≈ 40,000 | |
| **Estimated used** | **≈ 462 KB** | **≈ 58 KB free** |

**Decision gate in M2:** measure free heap with Wi-Fi connected. If headroom is below ~24 KB, take one of these fallbacks:

1. Do not store the 40 px toolbar in the framebuffer (saves ~38 KB).
2. Reduce the undo ring to 32 KB.
3. Switch to an 8-bit indexed framebuffer (153.6 KB) with a 256-entry RGB666 palette (saves ~153 KB, costs colour fidelity).
4. Reduce canvas to 400×240 and upscale 1.2× (last resort).

Plan for (1) + (2) first; they are transparent to the user.

### 5.4 Render & input pipeline

```
loop():
  touch.poll()          // every 4–5 ms, no blocking delay
  app.update()          // state machine, gestures, tool actions
  app.draw()            // draw only what changed into fb, mark dirty rects
  display.flush(dirty)  // merge rects, expand RGB565→RGB666, transfer in bands
  net.loop()            // Wi-Fi/OTA housekeeping, never blocks painting
```

- **Framebuffer first.** All drawing and UI composition happens in RAM; the panel is write-only.
- **Dirty rectangles.** A stroke marks only its bounding box; a button marks its own rect. Disjoint rectangles are kept separate (up to eight) and flushed independently, so a top-bar button and a bottom sheet do not force a full-height bounding box. Full-screen flushes happen only on screen transitions.
- **Banded transfer.** Split large dirty areas into ≤32-row bands so the watchdog is fed and the UI can respond between bands.
- **RGB666 expansion.** For each row: convert RGB565 → 3 bytes `[R6<<2, G6<<2, B6<<2]`, then one SPI write. Start blocking; add DMA only if profiling demands it.
- **Shape previews use XOR.** Draw the preview by XOR-ing pixels in the framebuffer, erase by XOR-ing the same pixels again. No background copy needed for screen-spanning shapes.
- **No dynamic allocation in the draw loop.** All buffers allocated once at boot.

### 5.5 Undo/redo design

- Divide the canvas into 32×32 tiles (10×14 = 140 tiles for 320×432).
- On the first write to a tile during an action, copy the tile into the current action buffer.
- Cap the whole undo ring at 51 tile records (~64 KB) and allow one action to use all of them; snapshot tiles lazily and evict the oldest action when the ring is full. Typical small strokes use 1–3 tiles, so ~10 actions fit at normal use.
- Flood fill / clear can touch every tile. For those, store a compressed RLE snapshot of the **pre-action** canvas instead; flat areas compress extremely well. If the snapshot exceeds the budget, disable undo for that one action and show a toast.
- On undo/redo, restore pixels and mark the affected bounding box dirty.
- Free the undo buffer during OTA to make room for network buffers.

### 5.6 Performance targets

| Metric | Target |
|---|---|
| Touch sampling | 100 Hz |
| Touch-to-pixel latency | < 40 ms |
| 8 px brush continuous stroke | ≥ 30 fps |
| UI button feedback | < 50 ms |
| Full-screen repaint @ 40 MHz | ≤ 150 ms |
| 64×64 dirty rect flush | ≤ 5 ms |
| Save (typical drawing) | < 500 ms |
| Load | < 1 s |
| Boot to canvas | < 2.5 s (Wi-Fi connects in background) |
| Free heap idle | ≥ 32 KB |
| OTA (400 KB firmware) | < 60 s on LAN |

SPI clock: start at **27 MHz**, validate at **40 MHz**, optionally try **62.5 MHz** with short wiring. ILI9488 datasheet timing is conservative; most modules run 40 MHz reliably.

---

## 6. Milestones

### Milestone summary

| ID | Milestone | Outcome | Size | Depends on |
|---|---|---|---|---|
| M0 | Hardware identification & bench bring-up | Confirmed module, pin map, backlight, touch | S (0.5–1 d) | — |
| M1 | Project skeleton & build config | Builds, flashes, filesystem mounts, CI green | S (0.5 d) | M0 |
| M2 | Display driver & graphics | Correct pixels + benchmark | M (2–3 d) | M1 |
| M3 | Touch input & UI shell | Navigable, calibrated UI | M (2–3 d) | M2 |
| M4 | Drawing engine | Brush/eraser, palette, undo | M (2 d) | M3 |
| M5 | Tools | Shapes, fill, picker, previews | M (2 d) | M4 |
| M6 | Storage & gallery | Save/load/autosave/gallery | M (2 d) | M4 |
| M7 | Wi-Fi OTA | Console-driven wireless updates | S/M (1–2 d) | M6 |
| M8 | Settings & polish | Complete UX + docs | S (1–2 d) | M5–M7 |
| M9 | Hardening & release | Tests, soak, CI, release | M (1–2 d) | All |

Estimates are rough solo-developer days and assume hardware is on the bench.

---

### M0 — Hardware identification & bench bring-up

**Goal:** eliminate hardware unknowns before committing to a pin map and driver.

- [x] M0-1 Photograph the module front/back and read the touch-controller marking — **U2 = HR2046** (XPT2046-compatible).
- [x] M0-2 Confirm the interface: 14-pin 4-wire SPI, 480×320 — confirmed from silkscreen `4.0" TFT SPI 480*320 V1.0`.
- [x] M0-3 Verify `LED` is a control input, measure total backlight current, and decide direct PWM vs MOSFET wiring. *Control input confirmed: 10k from `LED` to 3V3 lights the panel; onboard Q1 driver; GP21 PWM with no external MOSFET. Total current measurement deferred (not needed for the control decision).*
- [x] M0-4 Verify the Pico 2 W board: flash size, USB serial, `LED_BUILTIN` blink. *USB CDC serial and 4 MB flash confirmed; `LED_BUILTIN` heartbeat is in the M1/M2 firmware.*
- [x] M0-5 Wire display + touch per §4.2 on a breadboard; keep leads short. *Display, backlight and touch wired; all four touch corners respond.*
- [x] M0-6 Flash a minimal diagnostic sketch: backlight on/off, read raw touch values, print them over serial. *Display diagnostics done; the M3 build prints calibrated touch events, and `pico2w_diag` prints raw samples.*
- [x] M0-7 Record the confirmed wiring and any deviations in `docs/wiring.md`. *Display, backlight and touch sections confirmed.*

**Acceptance:** documented module variant and pin map; backlight controllable; raw touch values change with finger position; nothing gets hot.

---

### M1 — Project skeleton & build configuration

**Goal:** a reproducible build/flash/test setup for Pico 2 W, including the OTA target.

**Status (2026-10-24):** build configuration complete, compiling, flashed and hardware-verified.

- [x] M1-1 Update `platformio.ini`: `board = rpipico2w`, `framework = arduino`, and the RP2350-capable platform fork pinned to a commit (the official `platformio/raspberrypi` platform only ships RP2040 boards):
  `platform = https://github.com/maxgerhardt/platform-raspberrypi.git#5d4561a05e3b212660ac6fdd3fbfb328d1988aa1`
- [x] M1-2 Add `board_build.filesystem_size = 2m`; verify LittleFS reports the expected size. *On-device: `total=2097152 used=8192 free=2088960`.*
- [x] M1-3 Add environments:
  - `pico2w_usb` — normal USB upload (`upload_protocol = picotool`)
  - `pico2w_ota` — `upload_protocol = espota`, `upload_port = ${sysenv.PAINT_OTA_IP}`, `upload_flags = --auth=${sysenv.PAINT_OTA_PASSWORD}` (`espota` already receives `-p 2040` from PlatformIO)
  - `native` — host unit tests, `test_framework = unity`
  - `pico2w_diag` — optional hardware diagnostics build
- [x] M1-4 Add `include/config.h` (pins, dimensions, limits) and `include/version.h` (`PAINT_VERSION`, build timestamp).
- [x] M1-5 Add `include/secrets.example.h` and a runtime fallback when `secrets.h` is absent (empty SSID → offline mode).
- [x] M1-6 Extend `.gitignore`: `include/secrets.h`, `.pio/`, build artifacts, `.idea/`.
- [x] M1-7 Create the directory skeleton from §5.2 and a minimal `main.cpp` (serial banner + splash colour). *Banner, heartbeat and M2 diagnostic splash are implemented.*
- [x] M1-8 Initialise git and make the first commit (recommended; enables version stamping later).
- [x] M1-9 Add a CI workflow (GitHub Actions, if the project lives on GitHub): build `pico2w_usb`, run `native` tests, upload UF2/BIN artifacts.
- [x] M1-10 Document build/flash/OTA commands in `README.md`.

**Acceptance:** `pio run -e pico2w_usb` succeeds; UF2 flashes; serial banner appears; LittleFS reports ≈2 MB (`LittleFS.info()`); `pio test -e native` runs; `pico2w_ota` compiles. *All verified: USB flash, banner, 2 MB LittleFS, 8/8 native tests under WSL (Windows host lacks gcc), `pico2w_ota` builds.*

---

### M2 — Display driver & graphics primitives

**Goal:** correct, reasonably fast pixels on the ILI9488.

- [x] M2-1 Implement `Ili9488` driver: reset, init sequence, sleep-out, `COLMOD = 0x66` (18-bit), inversion setting, display-on. *Implemented following the TFT_eSPI ILI9488 sequence; the extra `0x21` inversion command was removed because `0xB4=0x02` already handles inversion. Hardware pending.*
- [x] M2-2 Implement rotation via MADCTL (test all four values); default **portrait 320×480**. *Implemented: `0xC8` (portrait 180°), `0x28` (landscape), `0x88` (portrait upright), `0xE8` (landscape 180°). The module is mounted 180° from the native portrait view, so the default is `config::kDisplayRotation = 2` (`0x88`). M8 uses rotation `2` and the true 180° `0xC8` value; the two landscape values are retained in the driver but are not exposed by the UI. The upright and flipped portrait values still need a visual check on hardware.*
- [x] M2-3 Implement `setAddrWindow(x, y, w, h)` and `pushPixelsRGB565()` with RGB565→RGB666 conversion in a line buffer. *Implemented as `setAddrWindow` + `flush` with a 480×3-byte line buffer.*
- [x] M2-4 Implement `gfx`: fillRect, hline/vline, Bresenham line, circle, clipped blit, 5×7 bitmap text (integer scaling). *Implemented; font is the classic Adafruit 5×7 table (BSD).*
- [x] M2-5 Implement dirty-rect merging and banded flush with watchdog feeding. *`DirtyRegion` now keeps up to eight disjoint rectangles (merging overlaps), and each is flushed independently with `rp2040.wdt_reset()` every 16 rows. The UI uses this in M3; canvas integration lands in M4.*
- [x] M2-6 Build the diagnostics screen: colour bars, grey ramp, checkerboard, orientation test, text sample. *Served its purpose during M2 and was retired when the UI shell replaced it. `pico2w_diag` still runs the benchmark paths before the UI; the colour-bar screen is recoverable from git history.*
- [x] M2-7 Benchmark: full screen, 64×64 rect, 432 px line; log ms per operation. *Implemented in `pico2w_diag`; on-device numbers pending.*
- [ ] M2-8 Measure free heap with and without a Wi-Fi prototype; record the memory decision (§5.3). Re-check the gate in M7. *Without Wi-Fi: 143,360 B free after the 307,200 B framebuffer; Wi-Fi measurement waits for M7.*
- [x] M2-9 Tune SPI clock: 27 → 40 → optionally 62.5 MHz; keep the fastest stable value. *40 MHz verified stable on 2026-10-24. Full-screen framebuffer flush: 139.3 ms after conversion-loop optimization (27 MHz baseline: 200.8 ms). 62.5 MHz not attempted; 40 MHz meets the target with margin.*

**Acceptance:** correct colours and orientation; no tearing/offset; full-screen fill ≤150 ms at 40 MHz; text legible; benchmark numbers recorded in `docs/performance.md`. ✅ *Verified on 2026-10-24: correct colours, upright portrait (rotation 2), no offset/clipping, stable 40 MHz, full-screen flush 139.3 ms, 143,360 B free heap after the framebuffer. Individual `[bench]` rows remain optional.*

**Risk note:** this is the highest technical risk. If the panel shows colour or init problems, compare against a known-good ILI9488 init sequence before building UI on top.

---

### M3 — Touch input & UI shell

**Goal:** a calibrated, responsive UI that feels like a device, not a dev board.

- [x] M3-1 Implement `ITouch`; `Xpt2046Touch` on SPI1 at ~2 MHz (mode 0), median-of-3 + EMA filtering. *Implemented with pipelined Z1/Z2/X/Y reads, pressure threshold and EMA. Hardware-validated on 2026-10-24 at all four corners.*
- [x] M3-2 Implement 4-point calibration; solve and store a 6-coefficient affine transform (Q16.16) in `/config.bin`. *Implemented: affine least-squares fit, plausibility/residual checks, versioned 40-byte config with FNV-1a checksum and atomic temp-file replacement.*
- [x] M3-3 Implement the touch state machine: down / move / up, movement threshold, long-press, tap. *Implemented with debounce, 2 px move threshold, tap detection, one-shot long-press and native tests.*
- [x] M3-4 Build the immediate-mode UI core: layout, buttons, modal sheets, toast, hit testing. *Toggles and sliders are not used by the M3 screens; they will land with the M8 settings screen.*
- [x] M3-5 Add icons and the 5×7 font assets; establish a consistent 44×44 px minimum touch target. *Compact vector icons are drawn in `src/ui/ui.cpp`; the 1-bit `icons.h` structure is reserved for the M8 polished set. All top-bar targets are 44×44 px.*
- [x] M3-6 Implement screens: splash, canvas shell (top bar + blank canvas), menu sheet, placeholder gallery/settings. *Also includes tools, palette and brush-size sheets plus About and Recovery placeholders.*
- [x] M3-7 Add the cooperative main loop with a frame budget and a watchdog. *10 ms touch poll, immediate UI update, partial dirty flush, 1 ms idle delay; the banded display flush feeds the watchdog every 16 rows. Explicit frame-budget instrumentation is deferred to M4 profiling.*
- [x] M3-8 Add a touch-calibration screen with crosshairs and a "save" step. *Four inset crosshairs, per-press raw capture, fit validation and automatic save; Cancel returns to Settings.*
- [x] M3-9 Verify calibration survives reboot and works in all four rotations (or lock to one). *Verified on hardware 2026-10-24: the first boot saved `/config.bin`; the reboot loaded and applied it. Rotation stays locked to 2.*

**Acceptance:** touch accuracy ±2 px at screen centre; no stuck/double taps; calibration persists; UI interaction <50 ms; splash → canvas <2.5 s.

**Status (2026-10-24): complete.** Touch accuracy, gesture handling, UI shell and calibration persistence are verified on hardware. Generic button/toast flushes are <12 ms; full-screen transitions are ~139 ms and the large palette/menu sheets remain 37–101 ms, deferred as a performance refinement rather than an M3 blocker.

---

### M4 — Drawing engine

**Goal:** the core paint experience.

- [x] M4-1 Implement `Canvas` (RGB565 framebuffer, clip rect, dirty tracking). *`Canvas` is a clipped view over the shared framebuffer with `setTarget`, `fillRect`, `setPixel`, bounded dirty tracking and direct flush support.*
- [x] M4-2 Precompute round brush masks for sizes 1, 2, 4, 8, 16, 24, 32 px. *1-bit header-only masks generated once in `brush.h`; nearest size wins.*
- [x] M4-3 Stamp brushes along interpolated segments (no gaps at fast swipes); support fractional stepping. *`strokeBrush` steps at `max(1, diameter/8)` and always stamps both endpoints; native tests verify continuity and clipping.*
- [x] M4-4 Implement eraser (paints paper white) with the same sizes. *The eraser uses the same brush masks and `config::kPaperColor`; enabled in the canvas UI.*
- [x] M4-5 Build the palette: 24 default colours + recently used; 6×4 swatch grid. *Five recent colours occupy the palette title row; selecting a recent swatch moves it to the front.*
- [x] M4-6 Implement the HSV colour picker (hue slider + saturation/value square) with a live preview. *Bottom sheet with hue bar, 32×32-cell S/V square, preview, H/S/V readout, Cancel/Use; tapping a control repaints only its affected marker bounds plus preview/readout (clipped cell drawing), not the whole sheet.*
- [x] M4-7 Implement undo/redo per §5.5 (tile snapshots, action ring, toast when an action is too large). *Fixed ~64 KB pool with 44 RLE + 7 raw tile records, up to 16 actions; captures per dab, converts to XOR deltas at commit, evicts the oldest action when full, and toasts when an action cannot be captured.*
- [x] M4-8 Implement "New / Clear canvas" with confirmation. *Menu → New canvas opens `Sheet::ConfirmNew`; Cancel returns, Clear requests a canvas reset, clears undo/redo and shows a toast.*
- [x] M4-9 Add brush-size picker with visual circles. *Size sheet with seven circles and labels; top-bar size icon reflects the active diameter.*
- [x] M4-10 Profile: stroke latency, fps for 1/8/24 px brushes, undo memory high-water mark. *Hardware-profiled 2026-10-24: 1 px ~139–152 µs/frame, 8 px ~143–153 µs, 24 px ~648–681 µs; first-dab flush 0.33–1.0 ms; 5 dots = 11 records / 1,372 B; heap flat at 57,008 B (normal) and 56,904 B (diag) for 5+ minutes. Long 8/24 px squiggles hit the 32-tile cap; HSV taps 90–103 ms. Follow-ups M4-11/M4-12.*
- [x] M4-11 (follow-up) Raise or restructure the per-action undo tile cap. *Implemented: `kMaxTilesPerAction` is the full 51-record pool (up from 32), so the newest long stroke can evict older actions instead of being refused. Native tests cover a 40-tile action and the 52-tile overflow path. Static RAM rises by ~1.2 KB (measured build +784 B overall); a hardware re-run of the 3.5 s 8 px / 4.2 s 24 px squiggles is pending.*
- [x] M4-12 (follow-up) Reduce HSV repaint cost. *Implemented: 32×32 S/V cells, hue/S/V marker-bound dirty regions, and clip-aware painter loops. An S/V tap now repaints ~16k pixels instead of ~49k; a hue tap ~50k instead of ~58k. Pre-change hardware numbers were hue 102.8 ms, S/V 90.3 ms, open 179.1 ms; re-measure on hardware. The full-sheet HSV open remains a ~179 ms overlay flush.*

**Acceptance:** continuous strokes with no gaps; 10 undo levels for normal strokes; 8 px brush ≥30 fps; free heap stable after 5 minutes of drawing.

**Status (2026-10-24): implemented; hardware validation pending.** Native tests and the host preview cover strokes, clipping, undo/redo, compressed/raw tile paths, New confirmation, HSV picking and recent colours. The compressed undo pool keeps ten short multi-tile actions in the 40-record-capacity case and no longer wastes a full 32×32 snapshot for every dab. Hardware latency/fps and heap high-water measurements remain to be recorded in `docs/performance.md`.

---

### M5 — Tools

**Goal:** complete the "Standard" tool set without compromising responsiveness.

- [x] M5-1 Line tool with XOR preview and commit on release.
- [x] M5-2 Rectangle tool (outline; optional filled variant in the tool options).
- [x] M5-3 Ellipse tool (outline; optional filled).
- [x] M5-4 Scanline flood fill (4-connected, exact RGB565 match, clipped to canvas).
- [x] M5-5 Eyedropper: sample canvas pixel, set as current colour, show a toast.
- [x] M5-6 Tool options row: size + filled/outline for shapes. *The enlarged tools sheet has a contextual row: Size opens the brush-size sheet, and Outline/Filled toggle filled rectangle and ellipse commits. Filled previews show the boundary while dragging (commit fills with one undoable action) to avoid a second framebuffer; outline/filled are disabled for non-shape tools.*
- [x] M5-7 Ensure every tool action is undoable (including fill via RLE snapshot).
- [x] M5-8 Handle edge cases: start/end outside canvas, drag over toolbar, rotation changes, interrupted touch. *`ui_layout::clampCanvasPoint()` clamps start/move points and is covered by native tests; releasing a shape over the toolbar cancels it instead of committing; opening a sheet or changing screen cancels any in-flight stroke/shape. Rotation remains locked to 2, so live rotation changes are out of scope until M8.*

**Acceptance:** preview leaves no residue after cancel; fill never escapes the canvas; picker returns the exact colour; all tools undo correctly.

**Status (2026-10-25): implemented; hardware validation pending.** The native suite covers shape preview/cancel, outline and filled commit/undo for rectangles and ellipses, flood fill inside/outside an outline and the no-op path, canvas clamping, the tool-options flow and the filled-mode toggle. Filled previews deliberately show only the boundary; commit fills and records one undo action. Re-run the tools, fill and options flow on hardware to confirm touch sizing and visual polish.

---

### M6 — Storage, autosave & gallery

**Goal:** drawings survive power loss and are easy to browse.

- [x] M6-1 Implement CRC32 and the RLE codec with unit tests (round-trip, worst case, corrupt input). *`paint/crc32.h` implements IEEE CRC-32 with incremental updates; `paint/codec_rle.{h,cpp}` encodes 16-bit run/literal tokens and strictly rejects zero-length tokens, truncation, overruns and trailing bytes. Native tests cover known vectors, incremental chaining, mixed runs/literals, an alternating worst case, capacity failure and corrupt streams.*
- [x] M6-2 Implement the `.pnt` format (Appendix A): header, RLE payload, embedded 64×86 thumbnail. *`paint/pnt.{h,cpp}` defines the 32-byte header (`PNT1`, version, flags, dimensions, RLE length, thumbnail offset, CRC32), streams row-chunked RLE plus a nearest-neighbour 64×86 thumbnail, and verifies magic/version/flags/reserved bytes/layout/CRC before decoding. Tests cover a pixel-identical 320×432 round-trip, thumbnail sampling and corrupt/truncated files.*
- [x] M6-3 Implement atomic saves (`.tmp` → rename) and refuse to overwrite a valid file with a corrupt one. *`storage/pnt_file.{h,cpp}` writes `/paint/imgNNNN.tmp`, flushes/closes, re-reads the header, size, CRC32 and RLE structure, and only then renames over the destination. A failed verification removes the temp file and leaves an existing good file untouched. `loadPnt()` verifies CRC before writing any canvas pixels. The atomic path was exercised on hardware as part of M6-10.*
- [x] M6-4 Implement save / save-as / load / delete with clear confirmation dialogs. *The menu separates Save and Save as. Save overwrites the current `imgNNNN.pnt`; an unnamed document and Save as both ask `PaintFileIndex::nextName()` for a free name, then show a Cancel/Save sheet. Gallery taps require Open confirmation and warn when the canvas is dirty; long-press requires Delete confirmation. Main owns the filesystem writes, updates the document name/dirty state and reports failures without leaving a half-written file.*
- [x] M6-5 Implement the gallery: paged 2×2 thumbnail grid, file names, empty state, long-press delete. *`storage/paint_files` scans and sorts up to 32 `.pnt` files; the UI renders 2×2 pages with names, prev/next arrows, a page counter and a "No drawings yet" state. Thumbnails are streamed from the file in 128 B source rows through a UI callback, so no 11 KB-per-thumbnail cache is needed. Long-press opens the delete dialog; a successful delete refreshes the grid and clears the document name if the open file was removed.*
- [x] M6-6 Implement autosave every 15 s after changes; recovery prompt on boot; delete autosave after a clean save. *`paint::AutosaveTimer` arms on the first change, writes `/paint/autosave.pnt` at most once per interval while changes are pending, and does not rewrite an idle dirty document. The gallery skips it. Boot validation reuses the header/dimensions/size/CRC/RLE checker; valid autosaves show Restore/Discard, restore as an unnamed dirty document, and are removed after a clean save, load, New canvas or explicit discard. Failed saves/recovery keep the file. Shape previews and active strokes are never autosaved. Recovery was exercised on hardware in M6-10; long-term flash wear remains a follow-up.*
- [x] M6-7 Implement free-space gauge and OTA headroom guard (keep ≥700 KB free for firmware staging). *`storage::StorageStats` exposes used/free space, used percentage and `otaHeadroom()` with `kOtaHeadroomBytes = 716,800`. `Storage` now wraps LittleFS mount/stats/format. Settings draws a live gauge (free KB, used percentage, OK/LOW reserve) and the Wi-Fi & OTA menu refuses below the reserve. Saves below the reserve still work but warn, so unsaved art can be rescued. M7 now gates the OTA server on the same threshold and lets `Update.begin()` reject an incoming image that cannot be staged.*
- [x] M6-8 Persist config: rotation, brightness, touch calibration, last-opened file, flags. *`storage/settings_codec.h` v2 uses a 44-byte payload: four one-byte fields (rotation, brightness, OTA enable, flags), the six int32 calibration coefficients and a zero-padded 16-byte bare filename. v1 28-byte configs migrate with empty `lastOpened` and zero `flags`. Successful saves/loads update the name; New, deleting the open file and recovery actions clear it. Rotation/brightness remain persisted for the M8 settings screen and are not applied by the public UI yet.*
- [x] M6-9 Add a storage diagnostics page (total/used/free, file count, mount status). *Settings → Storage details renders mount status, total/used/free KB, used percentage, the real picture count from `PaintFileIndex::totalCount()` and the OTA reserve. The count includes files beyond the 32-entry gallery index. The screen has a back path to Settings and refreshes storage stats on entry.*
- [x] M6-10 Test 20 save/load cycles and a simulated power cut during save. *The M6 build was flashed and the save/load/gallery, autosave recovery, storage gauge, config-migration and diagnostics flows were confirmed working by the hardware owner on 2026-10-26. Long-term flash wear and an instrumented timing run remain follow-ups, not blockers for M7.*

**Acceptance:** pixel-identical round-trip; corrupt files rejected without crashing; gallery opens images in <1 s; autosave restores after reset; no filesystem corruption after 20 cycles. **M6 hardware validation passed; the M7 work builds on the same storage paths.**

---

### M7 — Wi-Fi & OTA

**Goal:** update the device from PlatformIO over the LAN, with no web page and no USB cable.

**How it works (answering the OTA question):** arduino-pico includes `ArduinoOTA` and an OTA bootloader. The sketch starts Wi-Fi, calls `ArduinoOTA.begin()` and `ArduinoOTA.handle()`. PlatformIO then uploads with `upload_protocol = espota` exactly like an ESP32. The new image is staged in LittleFS, verified (MD5), and flashed by the bootloader on reboot.

- [x] M7-1 Implement a non-blocking Wi-Fi state machine (connect timeout 10 s, retry every 30 s, offline mode). *`Net` in `net/net.{h,cpp}` has Disabled/Offline/Connecting/Connected/OtaActive states, a 10 s connect timeout, 30 s background retries, optional static IP/DHCP, and no blocking waits. An empty SSID skips the wireless stack entirely and keeps the app offline; disabling the feature turns the radio off.*
- [x] M7-2 Add Wi-Fi status to the UI (off / connecting / connected + RSSI). *The Network screen shows off/connecting/connected/OTA-active/offline state, configured credentials, IP, hostname/port, RSSI, OTA-server state, reserve state and the last error. RSSI refreshes once per second and dirties only the `320×28` signal row.*
- [x] M7-3 Configure `ArduinoOTA`: hostname `pico-paint`, password from `PAINT_OTA_PASSWORD`, port 2040. *`Net::startServer()` sets the hostname from `PAINT_OTA_HOSTNAME`, port 2040 (`kOtaPort`) and the optional `PAINT_OTA_PASSWORD`; it logs `pico-paint.local:2040`. The server starts only after Wi-Fi association and stops on disable/disconnect.*
- [x] M7-4 Register callbacks: onStart (autosave, free undo memory, show overlay), onProgress (draw % bar), onEnd, onError (show message, return to canvas). *`onStart` closes any sheet, cancels an active stroke/shape, writes a dirty-document autosave, clears the fixed undo pool and paints the overlay. The start/progress paths preserve the framebuffer canvas (rather than composing paper over it), so a failed update returns to the drawing; the splash/recovery prompts stay visible underneath. `onProgress` repaints the `280×144` panel directly because `ArduinoOTA.handle()` blocks. `onEnd` paints 100% before reboot. `onError` records a flag in the callback (auth errors can run in the lwIP context); the main loop returns to the canvas with a toast. Autosave failure does not abort the update.*
- [x] M7-5 Add an OTA preflight: check free LittleFS space against the incoming size; refuse with a clear message if too low. *`Net` starts the OTA server only when free LittleFS is at least the existing `kOtaHeadroomBytes` (700 KB). The Network screen shows `OTA reserve: OK/LOW` and pauses the server when low. `Update.begin()` performs the final incoming-image check against the staging file; a refusal is reported through `OTA begin failed`. The current image is ~506 KB, so the reserve leaves staging headroom.*
- [x] M7-6 Add a "Wi-Fi & OTA" settings screen: IP, hostname, RSSI, server state, enable/disable toggle. *`Screen::Network` is opened by menu item 5 and shows all requested fields plus credentials, reserve and last OTA error. The toggle persists `Settings.otaEnabled`; it is disabled without credentials, during an OTA, and when enabling with low storage. Disabling stops the server and radio.*
- [x] M7-7 Wire up the `pico2w_ota` environment and document:
  ```bash
  export PAINT_OTA_IP=192.168.1.42
  export PAINT_OTA_PASSWORD='…'
  pio run -e pico2w_ota -t upload
  ```
  *`platformio.ini` uses `espota` with the environment-provided IP/password; PlatformIO's espota command already includes `-p 2040`, and `upload_flags` carries only `--auth=...` because PlatformIO forwards that value as one argument. `README.md`, `docs/ota.md` and `tools/ota_windows.sh` cover configuration, static IPs, the on-device server check, failure behaviour, security and recovery. On WSL2, use the helper because ArduinoOTA's reverse TCP connection cannot reach a WSL listener through the Windows LAN address. Set both variables for upload; a build without them still succeeds.*
- [x] M7-8 Add a version banner (`PAINT_VERSION`, build time) on splash/About and in the serial log. *The existing `version.h` banner remains in the serial log and About; the splash shows the version and the About stage text now reads `M8 settings + recovery`. `PAINT_OTA_HOSTNAME`, `PAINT_OTA_PASSWORD` and the static-IP fields are documented in `secrets.example.h`.*
- [ ] M7-9 Test 10 consecutive OTA updates; check drawings survive. *From WSL2, upload with `tools/ota_windows.sh`; the plain PlatformIO uploader authenticates but cannot receive the Pico's reverse TCP connection. A first OTA upload completed successfully through the helper on 2026-10-26; the ten-cycle run is still pending.*
- [ ] M7-10 Test failure paths: wrong password, Wi-Fi loss mid-update, power loss mid-update, insufficient space.
- [ ] M7-11 Verify USB recovery still works after a failed OTA.
- [x] M7-12 Document security: password-protected, LAN-only; optional signed firmware as a stretch goal. *`docs/ota.md` documents the OTA password, trusted-LAN requirement, the lack of signed firmware, and USB/BOOTSEL recovery. Signed firmware and HTTP/rollback remain future work.*

**Status (2026-10-26): M7-1–M7-8 implemented and native-tested; M7-12 documented; a first OTA upload is confirmed working through `tools/ota_windows.sh`; M7-9–M7-11 hardware validation remains.** The native suite is now **75/75** after the M8 additions (it was 70/70 at the M7 stage), including the Network-screen toggle and its disabled/low-storage paths. The three firmware environments build. Current sizes with a populated `secrets.h`: `pico2w_usb` RAM **164,692 B**, flash **505,940 B**; `pico2w_ota` RAM **164,692 B**, flash **505,940 B**; `pico2w_diag` RAM **164,792 B**, flash **507,204 B**. The estimated free heap is **31,916 B** after the framebuffer and overlay scratch; the boot log now prints `[mem] free heap before net` and `[mem] free heap` so the real wireless cost can be recorded. lwIP's 16,403 B `ram_heap` and 36,771 B PBUF pool are already in the static RAM figure.

**Acceptance:** wireless update succeeds without USB; drawings/config preserved; failed update does not brick the device; paint app works normally when Wi-Fi is unavailable; update completes in <60 s on LAN.

**Known limitation:** the arduino-pico OTA bootloader is power-fail safe but has no automatic application rollback. Mitigation: USB is always recoverable; add a boot-health counter (M8) that forces a recovery screen after repeated failed boots.

---

### M8 — Settings, polish & recovery

**Goal:** make it feel finished and recoverable.

- [x] M8-1 Settings screen: rotation, brightness, touch recalibration, Wi-Fi/OTA toggle, storage info, About. *Source complete. The full-screen Settings page has a 44 px rotation row (upright/flipped portrait only), four brightness steps (25/50/75/100%), the Wi-Fi/OTA toggle, Calibrate, Storage, About and Wi-Fi info. Rotation and brightness are saved to `/config.bin` and applied immediately. Touch calibration is still stored in canonical rotation-2 space and flipped for rotation 0.*
- [x] M8-2 Idle behaviour: dim backlight after 60 s, screen off after 5 min, wake on touch. *Source complete. `IdleController` dims at 60 s, turns the backlight off at 5 min and consumes the first press after off so it cannot activate a hidden control. Calibration, recovery tests and an active OTA keep the panel awake.*
- [x] M8-3 Boot-health counter: increment on boot, clear after 5 s of healthy running; after 3 bad boots show a recovery screen (skip Wi-Fi/autosave, offer diagnostics). *Source complete. `/boot.health` is an 8-byte `'P','B'`/version/count/CRC32 blob; three consecutive bad boots enter safe mode. A normal boot clears it after 5 s; Continue/Restart clear it from recovery. Safe mode skips Wi-Fi and autosave.*
- [x] M8-4 Splash screen with logo/version; skip on touch hold. *Source complete. The splash shows the palette mark, version, tagline and `Hold to skip`; the 800 ms timeout still guarantees boot, and a long-press skips it.*
- [x] M8-5 Error surfaces: filesystem mount failure, display init failure, low storage, OTA errors. *Source complete. Boot toasts cover `Storage unavailable`, `Display init failed` and `OTA reserve low`; OTA callbacks already surface `OTA auth failed`, begin/connect/receive/end errors. Saves below the reserve still succeed with an `OTA low` warning.*
- [x] M8-6 Polish visuals: consistent icon set, pressed states, modal dimming, toasts, no flicker on screen changes. *Source complete. Existing vector icons are reused, Settings/self-test rows have pressed and disabled states, gallery dialogs dim the background, and placeholder screens recompose once instead of flashing. Rotation on a placeholder recomposes the whole screen in the new orientation; on the canvas it re-flushes the preserved canvas framebuffer.*
- [x] M8-7 Add screenshots/UI mockups and a short user guide to `docs/`. *`docs/user-guide.md` covers boot, top bar, tools, saving, gallery, settings, idle, OTA and recovery. SVG mockups are in `docs/mockups/` and linked from the guide.*
- [x] M8-8 Accessibility pass: 44 px targets, high contrast, no colour-only status indicators. *Source complete. Primary targets are 44 px or larger, including the new Settings and recovery controls; storage/Wi-Fi/battery-style status is always written as text (`OK`/`FAIL`, `On`/`Off`, `Connected`, `OTA reserve: OK/LOW`) in addition to colour.*
- [x] M8-9 Add a "recovery mode" entry point (hold touch at boot) with display/touch/storage tests. *Source complete. Holding touch through power-on enters `Screen::SafeMode`; the screen offers Display test, Touch test (mapped and raw coordinates), Storage details, Continue and Restart. The same screen is used for boot-health recovery.*

**Status (2026-10-26): M8-1–M8-9 source complete and native-tested; M8 hardware acceptance remains pending.** The native suite passes **75/75** with new orientation/idle tests, boot-health codec/threshold tests, Settings navigation/request tests, safe-mode self-test tests and 44 px target checks. Builds: `pico2w_usb` RAM **164,692 B**, flash **505,940 B**; `pico2w_ota` RAM **164,692 B**, flash **505,940 B**; `pico2w_diag` RAM **164,792 B**, flash **507,204 B**. The source is not a hardware sign-off: rotation 0 (true 180° MADCTL), touch mapping after rotation, brightness/idle wake, persistence, boot-health counting, safe-mode entry and all three self-tests still need a device run.

**Acceptance (source):** all settings persist in `/config.bin`; idle dim/off/wake is implemented; docs and mockups are present; recovery mode is reachable by boot-health or hold-touch. **Hardware acceptance remains:** verify the same points on the Pico, including the flipped display/touch mapping and the boot-health loop.

---

### M9 — Hardening & release

**Goal:** confidence to call it v1.0.

- [ ] M9-1 Unit-test coverage for colour conversion, geometry, RLE, CRC, undo bookkeeping, calibration math, UI hit testing.
- [ ] M9-2 Fuzz the RLE decoder and file-header parser with truncated/garbage inputs.
- [ ] M9-3 Soak test: 1 hour of drawing + 50 saves + 10 OTA updates; log heap high-water mark.
- [ ] M9-4 Power-cycle test: 100 random resets, including during save and OTA.
- [ ] M9-5 Verify release build flags (no debug serial spam, watchdog on, OTA password required).
- [ ] M9-6 Produce release artifacts: UF2, OTA BIN, SHA256 sums.
- [ ] M9-7 Tag v1.0.0, publish GitHub release, attach artifacts.
- [ ] M9-8 Write `CHANGELOG.md` and a troubleshooting section (white screen, touch offset, OTA refused).
- [ ] M9-9 Final hardware check on a fresh Pico 2 W + fresh flash.

**Acceptance:** release checklist below is complete; no known blocking bugs; reproducible build from a clean checkout.

---

## 7. Testing strategy

### Host unit tests (`pio test -e native`)

- RGB565 ↔ RGB666/888, HSV conversion.
- Rect clipping, line/circle rasterisation, brush stamping continuity.
- RLE encode/decode round-trip + truncated/garbage input.
- CRC32 known vectors.
- `.pnt` header parse/serialise; corrupt magic/version/length.
- Undo tile bookkeeping and action eviction.
- Touch calibration affine solve + inverse mapping.
- UI hit testing and layout invariants.

### On-device diagnostics (`pico2w_diag`)

- Display: colour bars, grey ramp, orientation, alignment crosshairs.
- Touch: raw values, calibrated crosshair, 5-point accuracy, IRQ/polling check.
- Storage: mount, write/read/delete, free space, corrupt-file handling.
- Wi-Fi: scan/connect/RSSI, OTA server state.
- Memory: free heap, stack high-water, framebuffer checksum.

### Manual test checklist (per milestone)

- [ ] Cold boot with and without Wi-Fi credentials.
- [ ] Draw fast diagonal strokes; check for gaps.
- [ ] Undo/redo 20 times; check memory and visual correctness.
- [ ] Fill a large area, undo, redo.
- [ ] Save, power-cycle, load; compare pixel-for-pixel.
- [ ] Fill the gallery to the space guard; confirm warnings.
- [ ] Cut power during save; confirm recovery.
- [ ] OTA with correct/incorrect password; power-cut mid-update.
- [ ] Leave idle 10 minutes; confirm dim/wake.

### CI

- Build `pico2w_usb` + `pico2w_diag`.
- Run `native` tests.
- Cache PlatformIO packages.
- Upload artifacts on every push; publish on tags.
- Pin the platform version so OTA binaries are reproducible.

---

## 8. OTA details & operations

### PlatformIO environments (target shape)

```ini
[env:pico2w_usb]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git#5d4561a05e3b212660ac6fdd3fbfb328d1988aa1
board = rpipico2w
framework = arduino
monitor_speed = 115200
board_build.filesystem_size = 2m
build_flags =
  -DPAINT_BOARD_PICO2W=1
  -DPAINT_VERSION=\"0.1.0\"
upload_protocol = picotool

[env:pico2w_ota]
extends = env:pico2w_usb
upload_protocol = espota
upload_port = ${sysenv.PAINT_OTA_IP}
upload_flags = --auth=${sysenv.PAINT_OTA_PASSWORD}

[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17
```

Notes:

- First flash **must** be over USB (`pio run -e pico2w_usb -t upload`) with `ArduinoOTA` compiled in; every later update can be wireless.
- `upload_port` can be an IP or `pico-paint.local`; IP is more reliable on Windows.
- PlatformIO's espota uploader already passes `-p 2040`, and it forwards the whole `upload_flags` value as one argument. Keep `upload_flags` to the single `--auth=...` option; do not add a second flag there.
- The OTA image is the normal `.bin`; no special build is needed, but the filesystem must have room to stage it.
- Never OTA a filesystem image in production — it would erase the gallery. Sketch-only updates preserve LittleFS.

### OTA safety rules

1. Autosave the current drawing before accepting an update.
2. Free the undo buffer to maximise heap.
3. Refuse updates when free LittleFS space < firmware size + 128 KB.
4. Show a full-screen "Updating — do not power off" overlay with progress.
5. On error, return to the canvas (or show recovery) without rebooting unnecessarily.
6. Keep USB/BOOTSEL access physically reachable.
7. Use a non-trivial OTA password; OTA is LAN-only. Signed firmware is a stretch goal.

### Versioning

- `PAINT_VERSION` in `include/version.h`; bump manually for now.
- Add build date/time; show both in About and serial log.
- Later: inject `git describe --tags --dirty` via an extra script when the repo has tags.

---

## 9. Risks & mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| HR2046 clone quirks (noise, timing) | Low | Medium | Use the standard XPT2046 command set; test raw values and calibration in M0; median/EMA filtering |
| Touch panel edges less accurate than centre | Medium | Low | 4-point calibration; clamp to screen bounds; test all four corners |
| ILI9488 18-bit SPI is slow | High | Medium | Dirty rects, banded flushes, 40 MHz, optional DMA; avoid full-screen redraws |
| Not enough SRAM with Wi-Fi + framebuffer + undo | Medium | High | Memory gate in M2; drop toolbar from fb, shrink undo, 8-bit fallback |
| TFT_eSPI/RP2350 incompatibility | High if used | Low | Custom driver; no dependency |
| Long jumper wires corrupt SPI at 40 MHz | Medium | Medium | Short wiring, ground return, start at 27 MHz |
| OTA has no app-level rollback | Medium | Medium | Power-fail-safe bootloader, USB recovery, boot-health counter, autosave |
| Gallery fills LittleFS and blocks OTA | Medium | High | 700 KB headroom guard, free-space UI, warn/refuse saves |
| Flash wear from autosave | Low | Low | Autosave only on change, 15 s interval, atomic rename, wear-levelled LittleFS |
| Backlight current exceeds Pico regulator | Medium | Medium | MOSFET, measure current, external 5 V if needed |
| Touch jitter / false taps | Medium | Medium | Median + EMA filtering, movement threshold, calibration |
| Panel variant/orientation mismatch | Medium | Low | Runtime rotation setting, all four MADCTL values tested |
| PlatformIO board support changes | Low | Medium | Pin the maxgerhardt platform commit; CI builds |
| Wi-Fi credentials leak in repo | Medium | Medium | `secrets.h` gitignored, example file, CI uses placeholders |

---

## 10. Open questions

Resolved:

- **Touch controller:** HR2046 (XPT2046-compatible), confirmed from the module photo.
- **Orientation:** portrait 320×480.

Remaining:

| # | Question | Blocking? | Default if unanswered |
|---|---|---|---|
| 1 | Wi-Fi provisioning: compile-time `secrets.h` or on-device setup? | No | Compile-time for v1 |
| 2 | OTA always available when Wi-Fi is up, or menu toggle? | No | Always on, password-protected, toggle in settings |
| 3 | Gallery limit/delete policy: ask user or auto-prune oldest? | No | Ask user; refuse save when full |
| 4 | Physical button/reset access planned? | No | Design for BOOTSEL access |
| 5 | Is 4 MB flash confirmed? | No | Assume 4 MB |
| 6 | Are signed OTA updates required? | No | Password only in v1; signing in icebox |
| 7 | Backlight: PWM dimming required? | No | Yes, direct GPIO PWM into the module's LED control input (onboard driver); MOSFET only if the pin is raw LED power |

---

## 11. Icebox (post-v1 ideas)

- [ ] Layers with visibility/opacity.
- [ ] Pressure-sensitive brush size/opacity from XPT2046 Z.
- [ ] Zoom/pan and canvas resize.
- [ ] Text tool with an on-screen keyboard.
- [ ] Stamps, symmetry, pattern brushes.
- [ ] PNG/BMP export over Wi-Fi (download page).
- [ ] SD card storage and import/export.
- [ ] Screenshot-to-serial or Wi-Fi download.
- [ ] Themes and a dark UI.
- [ ] Polish localisation.
- [ ] Signed OTA firmware (RSA-2048) and automatic A/B rollback.
- [ ] GitHub-release auto-update (HTTP pull).
- [ ] Battery pack + charging + power management.
- [ ] 3D-printed enclosure/stand.

---

## 12. Release checklist (definition of done)

- [ ] All M0–M9 tasks checked or explicitly deferred.
- [ ] Clean build from a fresh checkout with pinned platform version.
- [ ] `pio test -e native` passes.
- [ ] 1-hour soak test: no crash, free heap stable, no visual corruption.
- [ ] 20 OTA updates succeed; 3 failure modes tested.
- [ ] Power-loss tests during save and OTA pass.
- [ ] Gallery round-trip is pixel-identical.
- [ ] Wiring diagram, README, OTA guide and troubleshooting docs complete.
- [ ] UF2 + OTA BIN + SHA256 published.
- [ ] `PAINT_VERSION` and changelog updated.

---

## Appendix A — `.pnt` file format (v1)

Little-endian.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `PNT1` |
| 4 | 2 | Format version (1) |
| 6 | 2 | Flags (reserved, 0) |
| 8 | 2 | Width (320) |
| 10 | 2 | Height (432) |
| 12 | 4 | RLE data length |
| 16 | 4 | Thumbnail offset |
| 20 | 4 | CRC32 of RLE data + thumbnail |
| 24 | 8 | Reserved (0) |
| 32 | … | RLE payload |
| … | 11,008 | 64×86 RGB565 thumbnail (raw) |

**RLE:** sequence of 16-bit tokens.

- Bit 15 set: run — length = `token & 0x7FFF`, followed by one RGB565 value.
- Bit 15 clear: literal — `token` pixels follow as RGB565 values.
- Decode stops when the pixel count reaches `width × height` or `data_len` is exhausted.

**Atomic write:** write `/paint/imgNNNN.tmp`, flush/close, then rename to `/paint/imgNNNN.pnt`. Never truncate an existing good file in place.

**Autosave:** `/paint/autosave.pnt`, written only after a change, every 15 s. Deleted after a clean save or explicit discard.

**Config:** `/config.bin` with magic `PCFG`, version 2, a 44-byte payload (rotation, brightness, OTA enable, flags, six calibration coefficients as int32, 16-byte bare last-opened filename) and an FNV-1a checksum. Version 1 28-byte payloads are still decoded and migrate with empty `lastOpened`/`flags`.

---

## Appendix B — UI specification

### Layout (portrait 320×480)

```
┌──────────────────────────┐ 0
│ [tool][size][color]      │
│            [undo][redo][☰]│ 48 px top bar
├──────────────────────────┤
│                          │
│                          │
│      CANVAS 320×432      │
│                          │
│                          │
└──────────────────────────┘ 480
```

- Top bar: 48 px high, six 44×44 px buttons (3 left, 3 right) with 4 px gaps and 18 px margins.
- Canvas: 320×432 px, paper white.
- Bottom sheets slide over the canvas: palette (≈220 px, 6×4 swatches), brush sizes (≈120 px), tools (≈180 px), menu (≈340 px).
- Modal sheets dim the canvas with a 50% checker/dither overlay (cheap, no alpha blending).
- Toasts appear 16 px above the bottom edge for 2 s.

### Screens

1. **Splash** — logo, version, 800 ms; hold touch to enter recovery.
2. **Canvas** — top bar + canvas; default screen.
3. **Palette sheet** — 24 swatches in a 6×4 grid + recent colours + "HSV" button.
4. **HSV picker** — horizontal hue slider + SV square + preview + OK/Cancel.
5. **Brush size sheet** — circles at 1/2/4/8/16/24/32 px.
6. **Tools sheet** — brush, eraser, line, rectangle, ellipse, fill, picker.
7. **Menu sheet** — New, Save, Gallery, Settings, Wi-Fi & OTA, About.
8. **Gallery** — 2×2 thumbnails (portrait aspect), page arrows, long-press delete.
9. **Settings** — rotation, brightness, calibration, storage, OTA toggle, reset.
10. **OTA overlay** — progress bar, percentage, "Do not power off".
11. **Recovery** — after repeated failed boots; display/touch/storage tests.

### Interaction rules

- Tap = down/up within 8 px and <400 ms.
- Drag = movement > 8 px; drawing begins immediately on down for pencil-like feel.
- Long-press = 600 ms (gallery delete, clear canvas).
- Shape tools preview with XOR; commit on release; cancel by dragging off-canvas or tapping the tool again.
- No gesture ambiguity: one finger only; ignore second touch.
- Every destructive action has a confirmation dialog.

### Visual style

- Flat, high-contrast, no gradients except the HSV picker.
- Paper white canvas, dark grey chrome, saturated tool colours.
- Icons as 24×24 1-bit bitmaps; labels in 5×7 font at 2× (10×14) and 3× for titles.
- Status icons: Wi-Fi (off/connecting/connected), storage warning, OTA ready.

---

## Suggested first week

1. **Day 1:** M0 — identify the module, wire it, confirm backlight and touch; write `docs/wiring.md`.
2. **Day 2:** M1 — retarget PlatformIO to `rpipico2w`, add envs, version header, CI, flash a splash.
3. **Days 3–5:** M2 — display driver, colour correctness, graphics primitives, benchmarks. Do not start UI until full-screen fill and rotation are solid.
4. **End of week:** review benchmark + memory numbers, then decide whether the full RGB565 framebuffer is viable or the 8-bit fallback is needed.
