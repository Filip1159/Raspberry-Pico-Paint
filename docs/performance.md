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

### M5 (shapes, fill, eyedropper)

After adding the header-only `ShapePreview`, rubber-band rasterisers, filled
shape spans and scanline flood fill, the M5 builds were:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 160,208 B (30.6%) | 430,212 B (20.6%) |
| `pico2w_diag` | 160,308 B (30.6%) | 431,492 B (20.6%) |

`ShapePreview` keeps a 2,048-entry saved-pixel buffer (`x/y/colour`, ~12 KB);
the rest is the rasterisers, the 1,024-point flood-fill stack and the M5 UI
options row.

### M6-4/M6-5 (save/load/delete + gallery)

Wiring the storage layer into the UI adds a bounded gallery index, the
document name/dirty state and the paged gallery:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 161,352 B (30.8%) | 439,300 B (21.0%) |
| `pico2w_ota` | 161,352 B (30.8%) | 439,300 B (21.0%) |
| `pico2w_diag` | 161,452 B (30.8%) | 440,588 B (21.0%) |

These are the intermediate M6-4/M6-5 figures; the later M6 sections have the
current builds. The static increase is ~1.1 KB: 32 gallery names in `Ui`, 32
names in `PaintFileIndex`, the document/request state and the extra
menu/dialog code.
The free-heap estimate is now:

```
524,288 - 161,352 (static) - 307,200 (framebuffer) - 20,480 (scratch)
= 35,256 B
```

That is still above the 24 KB Wi-Fi minimum. The M6-6 autosave reuses the
existing streaming save path and adds no pixel buffer. The gallery
deliberately allocates no thumbnail cache:
`drawPntThumbnail()` seeks to the embedded 64×86 thumbnail and streams the
source rows covered by the current 32-row compose band through a 128 B row
buffer. A page therefore costs at most a few hundred small flash reads, not
44 KB of RAM for four thumbnails.

The menu grew from six to seven rows (Save and Save as are separate) and
`menuItemRect()` now uses a 40 px pitch so all seven rows fit inside the
existing 340 px sheet.

### M6-6–M6-9 (autosave, recovery, storage + config)

The autosave scheduler, modal Recovery screen, storage gauge, settings schema
and diagnostics screen add no framebuffer-sized allocation. Final builds after
M6-9 are:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 161,404 B (30.8%) | 443,380 B (21.2%) |
| `pico2w_ota` | 161,404 B (30.8%) | 443,380 B (21.2%) |
| `pico2w_diag` | 161,504 B (30.8%) | 444,652 B (21.2%) |

M6-7 added 16 B of static RAM (`StorageStats` in `Ui`, plus alignment) and about
944 B of flash for the gauge, menu guard and warning paths. M6-8/M6-9 add
another 28 B of RAM and about 1.2 KB of flash for the config schema, the Storage
screen and the real file count.

The final free-heap estimate is:

```
524,288 - 161,404 (static) - 307,200 (framebuffer) - 20,480 (scratch)
= 35,204 B
```

`AutosaveTimer` is a few bytes of scheduler state. Autosave writes
`/paint/autosave.pnt` through the same 256 B RLE write buffer and atomic
`.tmp` rename as a normal save; it is deferred while a brush stroke or shape
preview is active, so rubber-band pixels are never captured. It runs only while
changes are pending: the first change arms a 15 s deadline, further changes do
not postpone it, and an idle dirty document is not rewritten.

Boot recovery validates with the existing 512 B RLE read buffer and the normal
header/size/CRC walk, then keeps the pixels on the canvas only after Restore.
No second canvas or full-file buffer is allocated. The save/load, gallery,
autosave-recovery and storage screens were hardware-validated after the M6
build; long-term flash-wear behaviour still needs a longer soak test. The write
cadence is bounded by the 15 s timer and stops once the document is clean.

The storage gauge reads `FSInfo` on boot, when Settings opens and after file or
autosave changes. `StorageStats` is 12 B (three 32-bit sizes); formatting the
gauge needs one small stack buffer, and a live update dirties only the
`320×96` gauge area instead of the whole Settings screen. The **700 KB**
(716,800 B) reserve is a policy check, not a hidden partition: pictures may
still be saved below it (with an `OTA low` warning) so unsaved art is not
stranded, while the OTA server stays stopped until space is freed.

The version 2 config blob is 56 B on flash (12 B header + 44 B payload), up
from 40 B; both load and save use a 56 B stack buffer. Version 1 files still
decode, so existing calibration survives the upgrade. The diagnostics screen
reuses `StorageStats` and the gallery refresh; `PaintFileIndex` adds one small
counter for files beyond the 32-entry index cap, and opening Storage does not
rescan the directory.

### M7 Wi-Fi + OTA (M7-1–M7-8)

Linking `WiFi`, lwIP, mDNS and `ArduinoOTA` adds a fixed wireless footprint.
The M7 builds are:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 164,640 B (31.4%) | 499,460 B (23.9%)¹ |
| `pico2w_ota` | 164,640 B (31.4%) | 499,460 B (23.9%)¹ |
| `pico2w_diag` | 164,740 B (31.4%) | 500,748 B (23.9%)¹ |

¹ Measured with the local populated `secrets.h`; without credentials the same
sources build about 104–112 B smaller.

That is +3,236 B of static RAM and about +56 KB of flash over the M6-9
`pico2w_usb` build. Most of the RAM is not allocated by the app: the arduino-pico
lwIP configuration reserves a **16,403 B** `ram_heap` and a **36,771 B** PBUF
pool at link time. Those pools are already included in the table, so they are
not a second 53 KB runtime allocation (the framework baseline and M6 figure
already carried most of this static footprint; do not add the pools twice).
The CYW43 driver may still allocate a small amount from the remaining heap for
control structures and mDNS strings.

The post-M7 free-heap estimate is:

```
524,288 - 164,640 (static) - 307,200 (framebuffer) - 20,480 (scratch)
= 31,968 B
```

This is above the 24 KB target but tighter than M6. The boot log prints
`[mem] free heap before net` and `[mem] free heap`; those values, plus the heap
after Wi-Fi association, must be recorded on hardware (M7-9–M7-11). If they are
too close to the limit, the first options are trimming the overlay scratch band
or lowering the lwIP pool size, not removing the undo pool.

The Network screen and OTA overlay add no framebuffer-sized buffer. `NetInfo`
is a small status struct and the UI repaints only changed rows:

- a status/toggle change dirties the `320×320` information area plus the button
  (about 93 ms at the measured full-screen rate);
- the once-per-second RSSI update dirties one `320×28` row (about 8 ms);
- storage updates dirty one `320×28` reserve row;
- entering the screen is a normal full-screen transition.

The overlay panel is `280×144` (about 36 ms per changed progress percent), and
`setOtaProgress()` ignores callbacks that do not change the integer percent.
`beginOta()` closes any open sheet. Outside the splash and boot-recovery
screens, the OTA start path switches to the canvas without composing the canvas
layer (which would replace the drawing with paper), then flushes the live top
bar and preserved canvas pixels. The boot screens keep their prompt visible
under the overlay so a failed transfer cannot discard it. Each
progress update composes the opaque panel over the canvas, flushes it, and
restores the covered framebuffer pixels, so a failed update leaves the drawing
intact. `onProgress` does this while `ArduinoOTA.handle()` blocks the normal
loop. On success the reboot follows; on failure the main loop restores the
canvas and shows a toast through the same overlay-safe path. Authentication
errors can be reported from the lwIP context, so `onOtaError` only sets a flag
and all display access remains in the main loop.

The native suite passes **70/70**, including the Network-screen toggle,
disabled-without-credentials, connected-request and low-storage paths. The
wireless failure paths and ten-update loop still require hardware.

### M8 settings, idle and recovery

M8 adds no framebuffer-sized or per-frame allocation. The current builds are:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 164,692 B (31.4%) | 505,940 B (24.2%) |
| `pico2w_ota` | 164,692 B (31.4%) | 505,940 B (24.2%) |
| `pico2w_diag` | 164,792 B (31.4%) | 507,204 B (24.2%) |

This is +52 B of static RAM and about +6.5 KB of flash over M7. The free-heap
estimate is:

```
524,288 - 164,692 (static) - 307,200 (framebuffer) - 20,480 (scratch)
= 31,916 B
```

The new state is small: the Settings UI keeps the current rotation, brightness
and pressed-button flags; `IdleController` is a timestamp plus an enum; the
boot-health blob is **8 B** on flash (`'P','B'`, version, count, CRC32) and is
written once per boot plus one clear after five healthy seconds. The recovery
screen, display test and touch test are normal UI compositions, not extra
buffers.

The Settings screen is now a full-screen compose with text-labelled rows; the
44 px target changes do not add per-frame work. Changing rotation while a
placeholder screen is open recomposes the 320×480 screen once because the
framebuffer would otherwise contain the old orientation. On the canvas,
rotation simply re-flushes the existing canvas framebuffer. Idle dim/off only
calls `setBacklight()`, so it costs no SPI payload; the first press while off
is swallowed to prevent accidental activation.

The native suite is now **75/75**. New coverage includes orientation mapping,
idle dim/off/wake thresholds, boot-health encode/decode/CRC/threshold, the
Settings rotation/brightness/Wi-Fi/About/Storage/Network/Calibrate navigation
and requests, the safe-mode display/touch/storage/Continue/Restart flow, and
44 px primary-target checks. Hardware validation of rotation, idle wake,
persistence, recovery entry and the self-tests remains pending.

### M6 storage foundation (M6-1–M6-3)

The CRC32/RLE codec, `.pnt` format and atomic-save path are now called from
the UI. The design deliberately avoids a framebuffer-sized save buffer:

- saves stream RLE in 512-pixel row chunks through a 256 B write buffer and
  patch the header lengths/CRC32 afterwards with `seek`;
- the raw 64×86 thumbnail is emitted one 128 B row at a time;
- loads verify the header, file size and CRC32 with a 512 B buffer, then walk
  the RLE stream a second time to fill the canvas (still 512 B);
- the worst-case 320×432 file is about 288 KB (two bytes per pixel + a token
  per row, plus the 11,008 B thumbnail and 32 B header); a blank canvas is
  dominated by the thumbnail.

CRC32 is a bitwise implementation, trading a small amount of save time for no
1 KB lookup table in flash. The save/load, gallery, autosave recovery, storage
gauge, config migration and diagnostics flows were hardware-validated after
the M6 build. Instrumented save/load timing and the long-term flash-wear soak
remain follow-ups.

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
| Records, actions, refs, scratch | ≈ 6,000 |

The pool holds up to 16 actions and 51 tile records per action (one action may
use the whole pool; older actions are evicted as needed). Each action
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
| Tools sheet open/close (M3, 320×180) | 44×44 button + 320×180 sheet | 52.3 ms sheet, **≈54 ms total**; the M5 options row makes the sheet 320×220, so expect ~64 ms until re-measured |
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
| Long 1 px stroke (3.9 s) | **31 records, 4,560 B** | now well below the 51-tile cap |
| Long 8 px and 24 px squiggles (3.5–4.2 s) | exceeded the old 32-tile cap → **"Stroke too large to undo"** | fixed in code; re-measure on hardware |

The 8 px and 24 px squiggles were deliberate long strokes across a large area;
they hit the old per-action cap of 32 tile records and were correctly rejected
rather than corrupting history. M4-11 raises the cap to the full 51-record
pool, and the native suite now covers a 40-tile action plus the 52-tile
overflow path. A hardware re-run is still needed to confirm the 4.2 s 24 px
squiggle now fits; if it touches all 51 tiles it can still overflow, but a
single action may now consume the whole pool instead of being refused at 32.

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
square **58.8 ms**, readout **20.5 ms**, preview **11.0 ms**. The square was the
dominant cost because the overlay band path re-entered the old 64×48-cell painter
for each band and the flush itself is a 216×156 RGB666 transfer.

M4-12 changes the repaint set and the painter:

- the S/V square is now 32×32 cells (half the `fillRect` calls);
- an S/V tap dirties only the 21×21 old/new marker bounds, preview and readout
  (~16k pixels) instead of the whole 216×156 square (~49k pixels);
- a hue tap dirties the square (its colours change), but only 7×30 bounds
  around the old and new hue markers instead of the full 280×30 bar;
- both painters skip rows/columns outside the clip.

A hue tap now asks for ~50k pixels of SPI payload instead of ~58k, and an S/V
tap for ~16k instead of ~49k. The S/V tap should be well under 50 ms; the hue
tap is close to the payload limit and still needs a hardware re-measurement.
Opening the 320×370 HSV sheet remains a full-sheet flush (~179 ms) and is not
addressed by M4-12.

The full-screen Gallery transition is **172.9 ms** on the overlay-band path,
versus **141.5 ms** for the return to the canvas. The canvas is preserved, but
the per-band compose adds overhead to full-screen overlays.

M4-11 and M4-12 are implemented; the hardware numbers above are the pre-change
baseline. Re-flash `pico2w_diag` and repeat the long-stroke and HSV taps to
record the new values.

The deterministic parts are covered by `pio test -e native` (75/75): canvas
clipping and dirty tracking, brush continuity, eraser/cancel, compressed and
raw undo deltas, ten-action retention across 40 tile records, a 40-tile
single action and overflow, filled shape commit/undo, HSV conversion, HSV tap
dirty bounds (including marker-only squares), tool options, canvas clamping,
New-canvas confirmation, undo/redo request handling, CRC32 known vectors and
incremental chaining, RLE worst-case/corrupt streams, a pixel-identical `.pnt`
round-trip with thumbnail/header/CRC rejection tests, paint-file name/path
helpers and autosave naming, the seven-row menu, Save/Save-as requests, the
gallery load/delete/paging/empty-state flows, the Storage details screen, the
15 s autosave schedule (including wraparound and non-postponing changes), the
storage used/free calculation and the 700 KB OTA-reserve boundary, the v2
config round-trip, v1 migration, invalid/autosave last-opened rejection, the
modal boot recovery Restore/Discard flows, the Network-screen toggle
(disabled without credentials, connected request and low-storage refusal),
orientation mapping, idle dim/off/wake, boot-health codec/threshold, the M8
Settings controls, the safe-mode self-tests and 44 px accessibility targets.

The overlay path was introduced in M4. Sheets, toasts, Settings, Gallery and
calibration compose into a 320-pixel-wide, 32-row scratch band, flush that
band, then restore the canvas pixels underneath. The cost is roughly the same
SPI payload as a normal dirty rectangle plus the compose work, but the canvas
is never destroyed. The HSV sheet is the largest new overlay
(320×370) and should be measured first on hardware. HSV taps dirty only the
moving marker bounds for S/V, the square plus marker bounds for hue, the
preview and the readout; the square/hue painters skip cells outside the clip,
so an adjustment does not re-flush the entire sheet.

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
