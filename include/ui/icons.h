#pragma once

// The M3 shell draws compact vector icons in src/ui/ui.cpp. This 1-bit
// bitmap structure is reserved for the polished M8 icon set.

#include <cstdint>

namespace paint::icons {

struct Icon {
  uint8_t width;
  uint8_t height;
  const uint8_t* data;
};

}  // namespace paint::icons
