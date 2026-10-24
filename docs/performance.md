# Performance notes

## Build footprint

Measured 2026-10-24 with the pinned RP2350 platform (M4 build):

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 138,032 B (26.3%) | 419,576 B (20.0%) |
| `pico2w_ota` | 138,032 B (26.3%) | 419,576 B (20.0%) |
| `pico2w_diag` | 138,132 B (26.3%) | 420,864 B (20.1%) |

The diagnostics build is ~100 B larger because it carries the profiling
counters and undo-usage reporting.

The 320×480 RGB565 framebuffer is allocated from the heap at boot
(307,200 B), and the overlay scratch is a 320×32 band (20,480 B). The M4
static RAM figure includes the fixed undo pool and UI state. The expected
free heap after both allocations is:

```
524,288 - 138,032 (static) - 307,200 (framebuffer) - 20,480 (scratch)
= 58,576 B
```

This is a budget estimate; the boot `[mem] free heap` line must be recorded on
hardware once the Pico is connected. For comparison, the pre-undo M3
calibration build reported **142,688 B free after the framebuffer** (allocator
overhead included; the reboot build reported 142,760 B; M2 reported 143,360 B).
The M4 undo pool costs about 64 KB of static RAM:

| Undo component | Bytes |
|---|---:|
| 44 RLE tile records × 1,024 | 45,056 |
| 7 raw tile records × 2,048 | 14,336 |
| Records, actions, refs, scratch | ≈ 4,700 |

The pool holds up to 16 actions and 32 tile records per action. Each action
captures the "before" pixels of the tiles it touches (RLE when compressible),
then converts them to XOR deltas at commit. Undo/redo XOR the same delta, so
one record serves both directions. The oldest action is evicted when either
pool fills; a single action that cannot be captured sets the overflow flag and
shows "Stroke too large to undo" instead of corrupting history.

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

The UI composes each dirty region into the shared RGB565 framebuffer, then
flushes the accumulated dirty rectangles. The M3 numbers below were taken
before the M4 overlay-band path; M4 reuses the same clipping/compose rules and
adds the 320×32 scratch band for opaque overlays:

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
sheet buttons, so the top-bar contribution is the normal button cost. When one
sheet fully contains the other (for example Palette → HSV, or Menu → New
canvas), only the larger sheet rectangle is marked; otherwise both sheet
rectangles are.

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

## M4 drawing and undo

Hardware validation was completed on **2026-10-24** with the `pico2w_diag`
build on COM16; the normal `pico2w_usb` build was also exercised manually for
drawing, undo and clearing. The serial log is the source for the numbers below.

### Heap

The normal build reported:

```
[fb] 320x480 RGB565 = 307200 bytes, free heap 77496
[ui] overlay scratch 320x32 = 20480 bytes, free heap 57008
[mem] free heap: 57008 bytes
```

The heap stayed at **57,008 B** through 135 s of drawing/undo/clear activity.
The diagnostics build, which carries the extra timing state, stayed at
**56,904 B** from 85 s to 605 s (the full test sequence plus idle time). The
budget estimate was 58,576 B; the ~1.5 KB difference is allocator overhead.
There is no per-stroke heap growth: the undo pool is fixed in the object.

### Stroke and undo

| Metric | Measured | Target / note |
|---|---:|---|
| 1 px stroke, first flush | **0.33 ms** | first dab after the drawing decision |
| 1 px stroke, steady flush | **139–152 µs**, 100 flushes/s | touch poll is 10 ms, so 100 Hz is the ceiling |
| 8 px stroke, first flush | **0.35 ms** | |
| 8 px stroke, steady flush | **143–153 µs**, 87–96 flushes/s | diagnostics serial output slightly lowers the rate |
| 24 px stroke, first flush | **1.0 ms** | |
| 24 px stroke, steady flush | **648–681 µs**, 78–93 flushes/s | still well above 30 fps |
| Stroke-to-pixel latency | **<1 ms** first flush, plus up to 10 ms touch poll | target <40 ms ✅ |
| Five 24 px dots | 5 actions, **11 tile records, 1,372 B** | compressed undo |
| Undo of a dot | **2.1–3.1 ms** canvas flush | |
| Long 1 px stroke (3.9 s) | **31 records, 4,560 B** | just below the 32-tile action cap |
| Long 8 px and 24 px squiggles (3.5–4.2 s) | exceeded 32 tiles → **"Stroke too large to undo"** | follow-up M4-11 |

The 8 px and 24 px squiggles were deliberate long strokes across a large area;
they hit the per-action cap of 32 tile records and were correctly rejected
rather than corrupting history. Shorter strokes and dots commit normally. This
is the main M4 usability follow-up: a normal multi-second squiggle can exceed
the cap. The native suite still verifies ten compressed actions across 40 tile
records.

### Overlays and UI flushes

| Event | Measured | Target |
|---|---:|---|
| Top-bar button | **2.6–5.7 ms** | <50 ms ✅ |
| Toast show/expire | **9.5 ms** | — |
| Size sheet open / close | **48.5 / 41.5 ms** | <50 ms (open borderline) |
| Palette open | **92.6 ms** | <50 ms ❌ |
| Menu open / close | **145.3 / 122.8 ms** | <50 ms ❌ |
| New-canvas confirm open | **76.2 ms** | <50 ms ❌ |
| New-canvas clear | **116.3 ms** + 9.5 ms toast | — |
| Gallery open / back | **172.9 / 141.5 ms** | — |
| HSV open | **179.1 ms** | <50 ms ❌ |
| HSV hue tap | **102.8 ms** | <50 ms ❌ |
| HSV saturation/value tap | **90.3 ms** | <50 ms ❌ |
| HSV Use close | **114.4 ms** + 9.5 ms toast | — |
| HSV Cancel close | **110.2 ms** | — |

The HSV hue tap breaks down as: S/V square **58.4 ms**, H/S/V readout
**20.7 ms**, hue bar **12.9 ms**, preview **10.8 ms**. The S/V tap is similar:
square **58.8 ms**, readout **20.5 ms**, preview **11.0 ms**. The square is the
dominant cost because the overlay band path re-enters the 64×48-cell painter
for each band and the flush itself is a 216×156 RGB666 transfer. Dirtying only
the affected controls works, but the 90–103 ms tap latency is still above the
50 ms UI target.

The full-screen Gallery transition is **172.9 ms** on the overlay-band path,
versus **141.5 ms** for the return to the canvas. The canvas is preserved, but
the per-band compose adds overhead to full-screen overlays.

**Follow-ups:** M4-11 covers long-stroke undo capacity; M4-12 covers HSV
repaint latency. Both are tracked in `BACKLOG.md`.

The deterministic parts are covered by `pio test -e native` (40/40): canvas
clipping and dirty tracking, brush continuity, eraser/cancel, compressed and
raw undo deltas, ten-action retention across 40 tile records, HSV conversion,
HSV tap dirty bounds, New-canvas confirmation and undo/redo request handling.

The overlay path was introduced in M4. Sheets, toasts, Settings, Gallery and
calibration compose into a 320-pixel-wide, 32-row scratch band, flush that
band, then restore the canvas pixels underneath. The cost is roughly the same
SPI payload as a normal dirty rectangle plus the compose work, but the canvas
is never destroyed. The HSV sheet is the largest new overlay
(320×370) and should be measured first on hardware. HSV taps dirty only the
hue paint bounds, the S/V square plus marker, the preview and the readout; the
square/hue painters skip cells outside the clip, so an adjustment does not
re-flush the entire sheet.

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
