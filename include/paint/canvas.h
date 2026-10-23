#pragma once

// Canvas framebuffer and dirty-rectangle tracking (implementation lands in M4).

#include <cstdint>

#include "paint/geometry.h"

namespace paint {

class Canvas {
 public:
  Canvas(uint16_t* framebuffer, int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  uint16_t* pixels() { return fb_; }

  void markDirty(const Rect& rect);
  Rect takeDirty();
  void clear(uint16_t color);

 private:
  uint16_t* fb_;
  int width_;
  int height_;
  Rect dirty_;
};

}  // namespace paint
