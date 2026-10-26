#!/usr/bin/env bash
# Upload a Pico Paint firmware image through Windows.
#
# ArduinoOTA authenticates over UDP, then the Pico opens a TCP connection back
# to the uploader. A listener inside WSL2 is not reachable through the Windows
# LAN address, so run espota.py with Windows Python instead. The WSL build
# output is shared on /mnt/c, so no rebuild is needed.
#
# Usage (from WSL):
#   export PAINT_OTA_IP=192.168.1.42
#   export PAINT_OTA_PASSWORD='...'
#   pio run -e pico2w_ota
#   tools/ota_windows.sh
set -euo pipefail

: "${PAINT_OTA_IP:?Set PAINT_OTA_IP to the device IP address}"
: "${PAINT_OTA_PASSWORD:?Set PAINT_OTA_PASSWORD to the OTA password}"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
firmware="$root/.pio/build/pico2w_ota/firmware.bin"
if [[ ! -f "$firmware" ]]; then
  echo "Missing $firmware" >&2
  echo "Build it first with: pio run -e pico2w_ota" >&2
  exit 1
fi

if ! command -v cmd.exe >/dev/null 2>&1; then
  echo "This helper needs WSL with Windows interop (cmd.exe)." >&2
  echo "On native Linux, use: pio run -e pico2w_ota -t upload" >&2
  exit 1
fi

win_home="$(wslpath -u "$(cmd.exe /c echo %USERPROFILE% 2>/dev/null | tr -d '\r')")"
espota="$win_home/.platformio/packages/framework-arduinopico/tools/espota.py"
if [[ ! -f "$espota" ]]; then
  echo "Missing $espota" >&2
  echo "Install the arduino-pico framework in Windows PlatformIO." >&2
  exit 1
fi

# The first interpreter already has a Windows firewall rule from PlatformIO.
if [[ -x "$win_home/.platformio/python3/python.exe" ]]; then
  win_python="$win_home/.platformio/python3/python.exe"
elif [[ -x "$win_home/.platformio/penv/Scripts/python.exe" ]]; then
  win_python="$win_home/.platformio/penv/Scripts/python.exe"
else
  win_python="$(command -v python.exe || true)"
fi
if [[ -z "$win_python" || ! -x "$win_python" ]]; then
  echo "No Windows Python found. Allow Python through Windows Firewall or" >&2
  echo "run PlatformIO once from Windows PowerShell." >&2
  exit 1
fi

echo "Uploading $firmware to $PAINT_OTA_IP:2040 via Windows"
exec "$win_python" "$(wslpath -w "$espota")" \
  -i "$PAINT_OTA_IP" -p 2040 --auth="$PAINT_OTA_PASSWORD" \
  --progress -f "$(wslpath -w "$firmware")"
