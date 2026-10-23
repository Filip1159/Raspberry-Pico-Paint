# Performance notes

## Build footprint

Measured 2026-10-24 with the pinned RP2350 platform:

| Environment | RAM | Flash |
|---|---:|---:|
| `pico2w_usb` | 72,216 B (13.8%) | 333,784 B (15.9%) |
| `pico2w_ota` | 72,216 B (13.8%) | 333,784 B (15.9%) |
| `pico2w_diag` | 72,216 B (13.8%) | 334,192 B (16.0%) |

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

The firmware prints these lines at boot:

```
[bench] fillScreen 320x480: ... us
[bench] 64x64 rect flush: ... us
[bench] 432px line flush: ... us
[bench] full-screen flush: ... us
[disp] diagnostics flush: ... us
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
| Diagnostics frame flush | 200.8 ms | **139.3 ms** | — |
| Free heap after framebuffer | **143,360 B** | — | ≥24 KB after Wi-Fi |

The 27 MHz value is from `[disp] diagnostics flush: 200792 us` on 2026-10-24,
before the conversion-loop optimization. The 40 MHz value is from
`[disp] diagnostics flush: 139334 us` on the same day, with the optimized
conversion loop. The panel was stable at 40 MHz with no tearing or corruption,
so the ≤150 ms M2 target is met.

The `pico2w_usb` build shows the diagnostics screen but does not print the
separate `[bench]` lines; flash `pico2w_diag` to collect the individual
fillScreen/rect/line numbers. A full-screen framebuffer flush is the relevant
number for the acceptance target.

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
