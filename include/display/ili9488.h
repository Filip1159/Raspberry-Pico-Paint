#pragma once

// ILI9488 SPI display driver (implementation lands in M2).
// The panel is write-only: display MISO is intentionally not connected.

#include <cstdint>

#include "paint/geometry.h"

namespace paint {

class Ili9488 {
 public:
  bool begin();
  void setRotation(uint8_t rotation);
  void fillScreen(uint16_t color);
  void flush(const Rect& rect, const uint16_t* pixels, int stride);

 private:
  uint8_t rotation_ = 0;
};

}  // namespace paint
