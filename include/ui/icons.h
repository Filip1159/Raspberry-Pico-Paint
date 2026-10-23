#pragma once

// 1-bit icon bitmaps live in flash. Definitions land with the UI in M3.

#include <cstdint>

namespace paint::icons {

struct Icon {
  uint8_t width;
  uint8_t height;
  const uint8_t* data;
};

}  // namespace paint::icons
