# OTA updates

Pico Paint uses arduino-pico's `ArduinoOTA` library and PlatformIO's `espota`
upload protocol. This is the same workflow as an ESP32: no web page and no USB
cable after the first flash.

## How it works

1. The sketch connects to Wi-Fi and starts an OTA server on port **2040**.
2. PlatformIO sends the new `.bin` with `upload_protocol = espota`.
3. The image is staged in LittleFS, verified (MD5), and flashed by the
   arduino-pico OTA bootloader on reboot.
4. The bootloader is power-fail safe: a complete, verified staged image is
   applied even if power is lost during the copy.
5. The gallery and `/config.bin` survive because only the sketch partition is
   updated.

The first flash **must** be over USB. Every later update can be wireless.

## Configuration

Copy the example secrets file and fill it in:

```bash
cp include/secrets.example.h include/secrets.h
```

```cpp
#define PAINT_WIFI_SSID "your-ssid"
#define PAINT_WIFI_PASSWORD "your-password"
#define PAINT_OTA_HOSTNAME "pico-paint"
#define PAINT_OTA_PASSWORD "use-a-real-password"

// Optional: leave 0.0.0.0 for DHCP.
#define PAINT_STATIC_IP "0.0.0.0"
#define PAINT_GATEWAY "0.0.0.0"
#define PAINT_SUBNET "255.255.255.0"
```

`include/secrets.h` is gitignored. The default `secrets.example.h` leaves the
SSID empty, so firmware built without a real `secrets.h` stays offline and the
Wi-Fi & OTA toggle is disabled.

The Wi-Fi state machine does not block painting:

- connect timeout: **10 s**;
- retry: every **30 s**;
- after a disconnect it retries in the background;
- if Wi-Fi is unavailable, all painting/storage features continue to work.

The **Wi-Fi & OTA** screen shows the connection state, whether credentials are
configured, the IP address, hostname/port, RSSI, OTA-server state, the 700 KB
staging reserve, the last OTA error and an enable/disable toggle. The toggle is
stored in `/config.bin` as `Settings.otaEnabled`. Disabling it turns the radio
off and stops the OTA server.

`upload_port` can be an IP address or `pico-paint.local` when mDNS works on
your network. An IP is more reliable on Windows.

## Upload

```bash
export PAINT_OTA_IP=192.168.1.42
export PAINT_OTA_PASSWORD='use-a-real-password'
pio run -e pico2w_ota -t upload
```

PlatformIO already passes `-p 2040` because `ArduinoOTA` listens on 2040. The
environment only supplies `--auth=...`: PlatformIO forwards the entire
`upload_flags` value as **one** argument, so a second option in that line ends
up inside the port value and `espota.py` fails before contacting the device. Use
an OTA password without spaces.

The device must show `OTA server: listening`. During an update it shows a
progress overlay; on success it reboots automatically. `onProgress` repaints
the overlay directly because `ArduinoOTA.handle()` blocks while the image is
being received. The overlay is drawn over a preserved canvas layer, so a failed
update returns to the drawing instead of a blank page. If the update starts
during the splash or boot-recovery prompt, that screen stays underneath; a
failed update keeps the recovery prompt available.

### Uploading from WSL2

`ArduinoOTA` authenticates over UDP, then the Pico opens a **TCP** connection
back to the uploader's LAN address. A listener inside WSL2 is not reachable at
the Windows LAN IP, so an upload started with `pio run -e pico2w_ota -t upload`
from WSL authenticates and then stalls at 0% until espota reports
`No response from device`.

Build in WSL, then run the helper so the listener lives on Windows:

```bash
export PAINT_OTA_IP=192.168.1.42
export PAINT_OTA_PASSWORD='use-a-real-password'
pio run -e pico2w_ota
tools/ota_windows.sh
```

The helper runs Windows Python's `espota.py` against the same
`.pio/build/pico2w_ota/firmware.bin`. Allow Python through Windows Firewall if
prompted. On native Windows or Linux, use `pio run -e pico2w_ota -t upload`
directly.

## Failure behaviour

| Failure | What happens |
|---|---|
| Wrong password | `OTA auth failed`; the update is rejected. The device stays on the current firmware. |
| Wi-Fi loss mid-update | `OTA connect/receive failed`; the overlay closes, a toast is shown and the canvas returns with the drawing intact. |
| Power loss mid-update | The bootloader only applies a complete, verified image. USB/BOOTSEL is always available for recovery. Three unsuccessful boots trigger the on-device safe mode; hold touch at power-on to enter it immediately. |
| Low LittleFS space | The OTA server is not started and the screen shows `OTA reserve: LOW` / `OTA server: paused`. Free space first. |
| Incoming image cannot be staged | The updater reports `OTA begin failed` and no image is flashed. |

Before accepting an update the firmware autosaves a dirty drawing, closes any
open sheet and clears undo history. The autosave is validated on the next boot
with the normal recovery prompt. A failed update does not erase the gallery or
configuration.

The **700 KB (716,800 B)** reserve is a free-space policy, not a hidden
partition. The current firmware image is about 506 KB; the margin covers the
staged image and filesystem overhead. Saves are still allowed below the reserve
(with an `OTA low` warning) so unsaved art can be rescued. The OTA server is
started only when the reserve is available, and `Update.begin()` performs the
final check against the actual incoming image.

## Security

- OTA is password-protected with `PAINT_OTA_PASSWORD`, using the `espota`
  challenge-response authentication. Do not reuse an important password.
- The device should be used on a trusted LAN; do not expose port 2040 to the
  internet.
- The firmware image is **not signed** by default. A party that can reach the
  OTA port and knows the password could install arbitrary firmware. Signed
  firmware is a possible follow-up, not a current guarantee.
- Keep USB/BOOTSEL access reachable. A failed or interrupted OTA may require a
  USB flash, especially after a power cut or a corrupt staged image.

## Troubleshooting

| Symptom | Check |
|---|---|
| Device not found | Same LAN, IP correct, Wi-Fi & OTA enabled, server shows `listening` |
| `Auth failed` | `PAINT_OTA_PASSWORD` matches the firmware and upload environment |
| `option --port: invalid integer value` | `upload_flags` contains more than one option; keep only `--auth=...` |
| `No filesystem` / update refused | Flash includes a LittleFS partition; free space ≥ 700 KB |
| `OTA reserve: LOW` | Delete gallery images or copy them elsewhere; the OTA server stays paused |
| Upload stalls | Wi-Fi signal, power supply, firewall/mDNS |
| Auth OK, then `No response from device` at 0% | Uploader is in WSL2; use `tools/ota_windows.sh` so the Pico can reach the TCP port |
| Device disappears after a failed update | Power-cycle; if it stays offline, reflash over USB in BOOTSEL mode |
| Device boots old firmware | Update did not finish; check the serial log and free space |
| Repeated recovery mode after updates | Run the recovery Display/Touch/Storage tests; Continue clears the boot-health counter, Restart reboots normally. If storage fails, reflash over USB. |
| Bricked after a failed update | Hold touch at power-on for recovery mode. If it does not respond, flash over USB in BOOTSEL mode; the bootloader should recover first. |

## PlatformIO environment

```ini
[env:pico2w_ota]
extends = env:pico2w_usb
upload_protocol = espota
upload_port = ${sysenv.PAINT_OTA_IP}
upload_flags = --auth=${sysenv.PAINT_OTA_PASSWORD}
```

`pio run -e pico2w_ota` still builds without the environment variables; set
`PAINT_OTA_IP` and `PAINT_OTA_PASSWORD` only when uploading. PlatformIO may
print an espota `upload_port` error line during a plain build when
`PAINT_OTA_IP` is unset; the build itself still succeeds.

## Validation status

The source implementation and native tests are complete. The security notes
above close M7-12. A first OTA upload has been confirmed working through
`tools/ota_windows.sh`; the following hardware runs remain (M7-9–M7-11):

- ten consecutive OTA updates, with a drawing restored after each boot;
- wrong-password, Wi-Fi-loss, power-loss and insufficient-space failures;
- USB recovery after a deliberately failed update;
- live free-heap, RSSI and display/SPI interaction measurements.

## Future

- Signed firmware (RSA-2048) via arduino-pico's signing support.
- HTTP pull updates from a version manifest.
- A/B rollback driven by the existing boot-health counter.
