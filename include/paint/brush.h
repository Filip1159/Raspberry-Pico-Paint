#pragma once

// Round brush mask generation and stroke interpolation (implementation in M4).

#include <cstdint>

namespace paint {

struct BrushMask {
  int radius = 0;
  int diameter = 0;
  const uint8_t* coverage = nullptr;  // 1 bit per pixel, row-major
};

}  // namespace paint
