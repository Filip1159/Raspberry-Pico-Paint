# OTA updates

Pico Paint uses arduino-pico's `ArduinoOTA` library and PlatformIO's `espota` upload protocol. This is the same workflow as an ESP32: no web page, no USB cable after the first flash.

## How it works

1. The sketch connects to Wi-Fi and starts an OTA server on port **2040**.
2. PlatformIO sends the new `.bin` with `upload_protocol = espota`.
3. The image is staged in LittleFS, verified (MD5), and flashed by the arduino-pico OTA bootloader on reboot.
4. The bootloader is power-fail safe: if power is lost during the update, it resumes the staged update on the next boot.

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
```

`include/secrets.h` is gitignored. If the SSID is empty, the firmware stays offline and OTA is disabled.

## Upload

```bash
export PAINT_OTA_IP=192.168.1.42
export PAINT_OTA_PASSWORD='use-a-real-password'
pio run -e pico2w_ota -t upload
```

The device IP is printed on the serial monitor and shown in the Wi-Fi & OTA screen (M7).

`upload_port` can also be `pico-paint.local` if mDNS works on your network. An IP is more reliable on Windows.

The environment passes `--port=2040` because ArduinoOTA listens on 2040, while PlatformIO's `espota` defaults to 3232. Keep that flag.

## Safety rules

- Keep at least **700 KB free** in LittleFS for firmware staging. The app refuses to start an update below this threshold.
- Autosave the current drawing before accepting an update.
- Never OTA a filesystem image in production — it would erase the gallery. Sketch-only updates preserve LittleFS.
- Keep USB/BOOTSEL access reachable for recovery.
- Use a non-trivial OTA password; OTA is LAN-only.
- The arduino-pico bootloader has no automatic application rollback. A boot-health counter (M8) forces a recovery screen after repeated failed boots.

## Troubleshooting

| Symptom | Check |
|---|---|
| Device not found | Same LAN, IP correct, OTA enabled, password matches |
| `Auth failed` | `PAINT_OTA_PASSWORD` matches the firmware |
| `No filesystem` / update refused | Flash size includes a LittleFS partition; free space ≥ firmware + reserve |
| Upload stalls | Wi-Fi signal, power supply, firewall/mDNS |
| Device boots old firmware | Update did not finish; check serial log and free space |
| Bricked after a failed update | Flash over USB in BOOTSEL mode; the bootloader should recover first |

## PlatformIO environment

```ini
[env:pico2w_ota]
extends = env:pico2w
upload_protocol = espota
upload_port = ${sysenv.PAINT_OTA_IP}
upload_flags = --auth=${sysenv.PAINT_OTA_PASSWORD}
```

## Future

- Signed firmware (RSA-2048) via arduino-pico's signing support.
- HTTP pull updates from a version manifest.
- A/B rollback with a health check.
