#pragma once

// Compile-time configuration shared by the whole firmware.
// Keep this file free of Arduino dependencies so native unit tests can use it.

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Secrets
// ---------------------------------------------------------------------------
// include/secrets.h is gitignored. Copy secrets.example.h to secrets.h and
// fill in your own values. Without it the firmware builds in offline mode.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  else
#    include "secrets.h"
#  endif
#else
#  include "secrets.example.h"
#endif

// Keep older/custom secrets.h files (which may predate the OTA/static-IP
// fields) buildable with safe defaults.
#ifndef PAINT_WIFI_SSID
#  define PAINT_WIFI_SSID ""
#endif
#ifndef PAINT_WIFI_PASSWORD
#  define PAINT_WIFI_PASSWORD ""
#endif
#ifndef PAINT_OTA_HOSTNAME
#  define PAINT_OTA_HOSTNAME "pico-paint"
#endif
#ifndef PAINT_OTA_PASSWORD
#  define PAINT_OTA_PASSWORD ""
#endif
#ifndef PAINT_STATIC_IP
#  define PAINT_STATIC_IP "0.0.0.0"
#endif
#ifndef PAINT_GATEWAY
#  define PAINT_GATEWAY "0.0.0.0"
#endif
#ifndef PAINT_SUBNET
#  define PAINT_SUBNET "255.255.255.0"
#endif

namespace paint::config {

// ---------------------------------------------------------------------------
// Panel / UI geometry (portrait)
// ---------------------------------------------------------------------------
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 480;
constexpr int kTopBarHeight = 48;
constexpr int kCanvasWidth = kScreenWidth;
constexpr int kCanvasHeight = kScreenHeight - kTopBarHeight;  // 432

// ---------------------------------------------------------------------------
// Display (SPI0)
// ---------------------------------------------------------------------------
constexpr uint8_t kDisplaySpi = 0;
constexpr uint8_t kDisplayCs = 17;
constexpr uint8_t kDisplayDc = 16;
constexpr uint8_t kDisplayRst = 20;
constexpr uint8_t kDisplayMosi = 19;
constexpr uint8_t kDisplaySck = 18;
constexpr uint8_t kBacklightPin = 21;

// 27 MHz was clean on the bench; 40 MHz is the M2 target. Fall back to
// 27 MHz if the panel shows corruption with the current wiring.
constexpr uint32_t kDisplaySpiHz = 40000000;

// The module is mounted 180 degrees from the panel's native portrait view.
// Rotation 2 is the normal upright portrait orientation for this build.
constexpr uint8_t kDisplayRotation = 2;

// ---------------------------------------------------------------------------
// Touch (SPI1) — HR2046, XPT2046-compatible
// ---------------------------------------------------------------------------
constexpr uint8_t kTouchSpi = 1;
constexpr uint8_t kTouchSck = 10;
constexpr uint8_t kTouchMosi = 11;
constexpr uint8_t kTouchMiso = 12;
constexpr uint8_t kTouchCs = 13;
constexpr uint8_t kTouchIrq = 14;
constexpr uint32_t kTouchSpiHz = 2'000'000;

// Gesture recognition. The touch driver already applies median + EMA
// filtering; these thresholds suppress the remaining finger jitter.
constexpr uint32_t kTouchPollMs = 10;
constexpr int kTouchMoveThreshold = 2;   // px before a move event is emitted
constexpr int kTouchTapMaxMove = 10;     // px of total travel still counted as a tap
constexpr uint32_t kTouchLongPressMs = 600;
constexpr uint8_t kTouchDebounceSamples = 2;

// ---------------------------------------------------------------------------
// Painting / memory
// ---------------------------------------------------------------------------
constexpr int kTileSize = 32;
constexpr int kUndoRingBytes = 64 * 1024;
constexpr int kMaxBrushSize = 32;
constexpr int kDefaultBrushSize = 8;
constexpr uint16_t kPaperColor = 0xFFFF;  // RGB565 white

// ---------------------------------------------------------------------------
// Settings / idle / recovery
// ---------------------------------------------------------------------------
constexpr uint8_t kDefaultBrightness = 255;
constexpr uint8_t kMinBrightness = 64;
// Four evenly spaced brightness steps keep the Settings screen simple and
// make every step visibly different on the panel.
constexpr uint8_t kBrightnessLevels[] = {64, 128, 192, 255};
constexpr size_t kBrightnessLevelCount =
    sizeof(kBrightnessLevels) / sizeof(kBrightnessLevels[0]);

constexpr uint32_t kIdleDimMs = 60u * 1000u;      // 60 s
constexpr uint32_t kIdleOffMs = 5u * 60u * 1000u; // 5 min
constexpr uint8_t kIdleDimDuty = 32;              // backlight duty while idle
constexpr uint32_t kBootHealthyMs = 5000;
constexpr uint8_t kBootFailThreshold = 3;

// ---------------------------------------------------------------------------
// Wi-Fi / OTA
// ---------------------------------------------------------------------------
// The defaults live in secrets.example.h and keep the SSID empty, which makes
// Net report "not configured" without touching the wireless stack. Copy the
// example to include/secrets.h to enable wireless OTA.
constexpr const char* kWifiSsid = PAINT_WIFI_SSID;
constexpr const char* kWifiPassword = PAINT_WIFI_PASSWORD;
constexpr const char* kOtaHostname = PAINT_OTA_HOSTNAME;
constexpr const char* kOtaPassword = PAINT_OTA_PASSWORD;
constexpr const char* kStaticIp = PAINT_STATIC_IP;
constexpr const char* kGateway = PAINT_GATEWAY;
constexpr const char* kSubnet = PAINT_SUBNET;
constexpr uint16_t kOtaPort = 2040;

}  // namespace paint::config
