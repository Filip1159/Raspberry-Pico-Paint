#pragma once

// ILI9488 SPI display driver.
// The panel is write-only: display MISO is intentionally not connected.
// The driver keeps an RGB565 framebuffer supplied by the caller and expands
// each line to RGB666 while transferring.

#include <cstddef>
#include <cstdint>

#include "paint/geometry.h"

namespace paint {

class Ili9488 {
 public:
  static constexpr int kMaxWidth = 480;

  bool begin();
  void setRotation(uint8_t rotation);
  void setSpiClock(uint32_t hz);
  void setBacklight(uint8_t duty);

  int width() const { return width_; }
  int height() const { return height_; }
  uint8_t rotation() const { return rotation_; }

  void fillScreen(uint16_t color);
  void flush(const Rect& rect, const uint16_t* pixels, int stride);

 private:
  void reset();
  void initSequence();
  void writeCommand(uint8_t command);
  void writeData8(uint8_t data);
  void writeData16(uint16_t data);
  void writeDataBuf(const uint8_t* data, size_t length);
  void setAddrWindow(int x, int y, int w, int h);
  void fillLine(uint16_t color, int pixels);

  uint8_t rotation_ = 0;
  int width_ = 320;
  int height_ = 480;
  uint32_t spiHz_ = 27000000;
  uint8_t line_[kMaxWidth * 3] = {};
};

}  // namespace paint
