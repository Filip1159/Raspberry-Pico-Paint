# Performance notes

## Build footprint

Measured 2026-10-24 with the pinned RP2350 platform:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 72,656 B (13.9%) | 343,200 B (16.4%) |
| `pico2w_ota` | 72,656 B (13.9%) | 343,200 B (16.4%) |
| `pico2w_diag` | 72,728 B (13.9%) | 343,704 B (16.4%) |

The 320×480 RGB565 framebuffer is allocated from the heap at boot
(307,200 B). The M1 firmware reported 452,472 B free before that allocation.
After the framebuffer allocation, the M2 firmware reports **143,360 B free**
(allocator overhead included).

## M2 display benchmarks

Build and flash the diagnostics environment, then open the serial monitor:

```bash
pio run -e pico2w_diag -t upload
pio device monitor -b 115200
```

The diagnostics build runs the benchmarks before starting the UI, then prints
the initial splash flush time:

```
[bench] fillScreen 320x480: ... us
[bench] 64x64 rect flush: ... us
[bench] 432px line flush: ... us
[bench] full-screen flush: ... us
[disp] splash flush: ... us
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
| Full-screen `fillScreen` | pending | pending | — |
| 64×64 rect flush | pending | pending | ≤5 ms |
| 432 px line flush | pending | pending | — |
| Full-screen framebuffer flush | 200.8 ms | **139.3 ms** | ≤150 ms @ 40 MHz ✅ |
| Splash / full-screen UI flush | 200.8 ms | **139.3 ms** expected | ≤150 ms |
| Free heap after framebuffer | **143,360 B** | — | ≥24 KB after Wi-Fi |

The 27 MHz value is from `[disp] diagnostics flush: 200792 us` on 2026-10-24,
before the conversion-loop optimization. The 40 MHz value is from
`[disp] diagnostics flush: 139334 us` on the same day, with the optimized
conversion loop. The panel was stable at 40 MHz with no tearing or corruption,
so the ≤150 ms M2 target is met. The M3 splash uses the same full-screen flush
path; its on-device `[disp] splash flush` value is still to be recorded.

The normal `pico2w_usb` build does not print the separate `[bench]` lines; flash
`pico2w_diag` to collect the individual fillScreen/rect/line numbers and the
per-event `[ui] flush` timings.

## M3 UI shell flushes

The UI composes the whole screen into the RGB565 framebuffer on each dirty
event, then flushes the accumulated dirty rectangles:

| Event | Dirty regions | Estimated total |
|---|---|---|
| Top-bar press/release | one 44×44 button | ≈1.7 ms |
| Toast show/expire | 288×36 toast | ≈9 ms |
| Tools sheet open/close | top bar + 320×180 sheet | ≈54 ms |
| Palette sheet open/close | top bar + 320×220 sheet | ≈66 ms |
| Size sheet open/close | top bar + 320×120 sheet | ≈37 ms |
| Menu sheet open/close | top bar + 320×340 sheet | ≈101 ms |
| Screen transition | full 320×480 | **139.3 ms measured** |

The estimates use the measured full-frame overhead factor (139.3 ms measured
vs 92.2 ms theoretical payload, ≈1.51×). The `DirtyRegion` keeps up to eight
disjoint rectangles separate, so the top bar and a bottom sheet are flushed
independently rather than as one full-screen bounding box. The exact
`[ui] flush` numbers must be measured on hardware. The tools, palette and menu
sheets still exceed the <50 ms UI-interaction target, so large sheet transitions
may need a partial-canvas strategy, a faster SPI clock, or a shorter sheet. Sheet
dimming is intentionally omitted for now because it would expand every sheet
flush to the full canvas above the sheet.

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
