# Performance notes

## Build footprint

Measured 2026-10-24 with the pinned RP2350 platform:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 72,768 B (13.9%) | 346,872 B (16.6%) |
| `pico2w_ota` | 72,768 B (13.9%) | 346,864 B (16.6%) |
| `pico2w_diag` | 72,840 B (13.9%) | 347,384 B (16.6%) |

The 320×480 RGB565 framebuffer is allocated from the heap at boot
(307,200 B). The M1 firmware reported 452,472 B free before that allocation.
After the framebuffer allocation, the M3 calibration build reports **142,688 B
free** (allocator overhead included; the reboot build reported 142,760 B; M2
reported 143,360 B).

## M2 display benchmarks

Build and flash the diagnostics environment, then open the serial monitor:

```bash
pio run -e pico2w_diag -t upload
pio device monitor -b 115200
```

The diagnostics build runs the benchmarks before starting the UI, then prints
the initial splash flush time:

```
[bench] fillScreen 320x480: 117748 us
[bench] 64x64 rect flush: 3851 us
[bench] 432px line flush: 304 us
[bench] full-screen flush: 139308 us
[disp] splash flush: 139303 us
[ui] flush 1/2 x,y wxh: ... us
```

The firmware now runs the display at the M2 target clock of **40 MHz**. The
theoretical payload time for a full 320×480 RGB666 frame is:

```
320 * 480 * 3 bytes / 40,000,000 bit/s = 92.2 ms
```

At the initial 27 MHz clock it was 136.5 ms. The measured value includes
per-row conversion and transfer overhead. The 27 MHz measurement below is the
baseline taken before the conversion loop was changed from a per-pixel helper
call to a direct pointer write. The optimized 40 MHz build measures 139.3 ms,
which meets the target.

| Measurement | 27 MHz | 40 MHz | Target |
|---|---:|---:|---:|
| Full-screen `fillScreen` | pending | **117.8 ms** | — |
| 64×64 rect flush | pending | **3.84 ms** | ≤5 ms ✅ |
| 432 px line flush | pending | **0.311 ms** | — |
| Full-screen framebuffer flush | 200.8 ms | **139.3 ms** | ≤150 ms @ 40 MHz ✅ |
| Splash / full-screen UI flush | 200.8 ms | **139.3 ms** | ≤150 ms ✅ |
| Free heap after framebuffer | **143,360 B** (M2) | **142,688 B** (M3 calibration) | ≥24 KB after Wi-Fi |

The 27 MHz value is from `[disp] diagnostics flush: 200792 us` on 2026-10-24,
before the conversion-loop optimization. The 40 MHz values are from the M3
hardware run on the same day. The panel was stable at 40 MHz with no tearing or
corruption, so the ≤150 ms M2 target is met. The M3 splash uses the same
full-screen flush path and measured 139,303 us.

The normal `pico2w_usb` build does not print the separate `[bench]` lines; flash
`pico2w_diag` to collect the individual fillScreen/rect/line numbers and the
per-event `[ui] flush` timings.

## M3 UI shell flushes

The UI composes the whole screen into the RGB565 framebuffer on each dirty
event, then flushes the accumulated dirty rectangles:

| Event | Dirty regions | Measured on hardware |
|---|---|---|
| Top-bar press/release | one 44×44 button | **1.85–1.98 ms** |
| Settings "Calibrate touch" | 280×44 button | **11.2 ms** |
| Toast show/expire | 288×36 toast | **9.4–9.5 ms** |
| Tools sheet open/close | 44×44 button + 320×180 sheet | 52.3 ms sheet, **≈54 ms total** |
| Palette sheet open/close | 44×44 button + 320×220 sheet | 63.9 ms sheet, **≈66 ms total** |
| Size sheet open/close | 44×44 button + 320×120 sheet | 34.9 ms sheet, **≈37 ms total** |
| Menu sheet open/close | 44×44 button + 320×340 sheet | 98.7 ms sheet, **≈101 ms total** |
| Calibration step | full 320×480 | **139.4 ms** |
| Screen transition | full 320×480 | **139.3 ms** |

The sheet times are the measured `[ui] flush` values plus the measured ~1.9 ms
button flush. The first hardware run marked the whole 320×48 top bar on a sheet
change, which cost ~13.9 ms; `Ui::setSheet()` now dirties only the old and new
sheet buttons, so the top-bar contribution is the normal button cost. Switching
directly between two sheets flushes two buttons plus both sheets.

The `DirtyRegion` keeps up to eight disjoint rectangles separate, so the top bar
and a bottom sheet are flushed independently rather than as one full-screen
bounding box. The tools, palette and menu sheets still exceed the <50 ms
UI-interaction target, so large sheet transitions may need a partial-canvas
strategy, a faster SPI clock, or a shorter sheet. Sheet dimming is intentionally
omitted for now because it would expand every sheet flush to the full canvas
above the sheet.

The guided calibration flow repaints the full screen when the crosshair advances
(four steps, each measured at ~139.4 ms). The first calibration run saved
`/config.bin`; a reboot loaded and applied it, logging `[cfg] touch calibration
applied`. This is acceptable for a one-time setup flow and keeps the crosshair
crisp.

Keep the fastest stable SPI clock in `include/config.h` (`kDisplaySpiHz`); if
40 MHz shows corruption, fall back to 27 MHz and note it here.

## Other targets

| Metric | Target |
|---|---|
| Touch sampling | 100 Hz |
| Touch-to-pixel latency | < 40 ms |
| 8 px brush continuous stroke | ≥ 30 fps |
| UI button feedback | < 50 ms |
| Save (typical drawing) | < 500 ms |
| Load | < 1 s |
| Boot to canvas | < 2.5 s |
| OTA (400 KB firmware) | < 60 s on LAN |
