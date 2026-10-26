#pragma once

// Persisted device settings. Calibration persistence landed in M3; the M6-8
// schema adds the last-opened file and feature flags, while the M8 settings
// screen is the public UI. Kept separate from include/config.h to avoid name
// confusion.

#include <cstdint>

#include "input/calibration.h"
#include "storage/paint_files.h"

namespace paint {

struct Settings {
  // Persisted display rotation. Only portrait 2 (upright) and 0 (180-degree
  // flip) are exposed by the UI; other values are normalized to 2 on load.
  uint8_t rotation = 0;
  uint8_t brightness = 255;     // 0..255, clamped to the supported levels
  bool otaEnabled = true;
  uint8_t flags = 0;            // reserved feature flags, persisted verbatim
  Calibration calibration;
  char lastOpened[kPaintFileNameSize] = {};  // bare "imgNNNN.pnt" or empty
};

bool loadSettings(Settings& out);
bool saveSettings(const Settings& settings);

}  // namespace paint
