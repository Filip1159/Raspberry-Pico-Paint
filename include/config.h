#pragma once

// Compile-time configuration shared by the whole firmware.
// Keep this file free of Arduino dependencies so native unit tests can use it.

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
#    include "secrets.example.h"
#  endif
#else
#  include "secrets.example.h"
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
// OTA
// ---------------------------------------------------------------------------
constexpr uint16_t kOtaPort = 2040;
constexpr const char* kOtaHostname = "pico-paint";

}  // namespace paint::config
