#pragma once

// Copy this file to include/secrets.h (gitignored) and fill in real values.
// Leaving the SSID empty keeps the firmware in offline mode.

#define PAINT_WIFI_SSID ""
#define PAINT_WIFI_PASSWORD ""

#define PAINT_OTA_HOSTNAME "pico-paint"
#define PAINT_OTA_PASSWORD ""

// Optional static IP configuration (leave 0.0.0.0 for DHCP).
#define PAINT_STATIC_IP "0.0.0.0"
#define PAINT_GATEWAY "0.0.0.0"
#define PAINT_SUBNET "255.255.255.0"
