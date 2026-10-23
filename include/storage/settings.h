#pragma once

// Persisted device settings (implementation lands in M3/M6).
// Kept separate from include/config.h to avoid name confusion.

#include <cstdint>

#include "input/calibration.h"

namespace paint {

struct Settings {
  uint8_t rotation = 0;         // 0 = portrait
  uint8_t brightness = 255;     // 0..255
  bool otaEnabled = true;
  Calibration calibration;
};

bool loadSettings(Settings& out);
bool saveSettings(const Settings& settings);

}  // namespace paint
