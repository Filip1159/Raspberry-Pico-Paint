#pragma once

// RGB565 painting canvas. The canvas is a clipped view over the main
// framebuffer, so drawing and UI composition share the same pixel store
// without a second full-screen buffer.

#include <cstdint>

#include "display/dirty.h"
#include "paint/geometry.h"

namespace paint {

class Canvas {
 public:
  Canvas() = default;
  Canvas(uint16_t* framebuffer, int stride, const Rect& bounds)
      : framebuffer_(framebuffer), stride_(stride), bounds_(bounds) {}

  void setTarget(uint16_t* framebuffer, int stride, const Rect& bounds) {
    framebuffer_ = framebuffer;
    stride_ = stride;
    bounds_ = bounds;
    dirty_.reset();
  }

  bool ready() const { return framebuffer_ != nullptr && !bounds_.empty(); }
  const Rect& bounds() const { return bounds_; }
  int stride() const { return stride_; }
  uint16_t* pixels() const { return framebuffer_; }

  void clear(uint16_t color) { fillRect(bounds_, color); }

  void fillRect(const Rect& rect, uint16_t color) {
    const Rect r = rect.intersect(bounds_);
    if (r.empty()) {
      return;
    }
    for (int y = r.y; y < r.bottom(); ++y) {
      uint16_t* row = framebuffer_ + y * stride_ + r.x;
      for (int x = 0; x < r.w; ++x) {
        row[x] = color;
      }
    }
    markDirty(r);
  }

  void setPixel(int x, int y, uint16_t color) {
    if (!bounds_.contains(static_cast<int16_t>(x), static_cast<int16_t>(y))) {
      return;
    }
    framebuffer_[y * stride_ + x] = color;
    markDirty({static_cast<int16_t>(x), static_cast<int16_t>(y), 1, 1});
  }

  uint16_t pixel(int x, int y) const {
    if (!bounds_.contains(static_cast<int16_t>(x), static_cast<int16_t>(y))) {
      return 0;
    }
    return framebuffer_[y * stride_ + x];
  }

  void markDirty(const Rect& rect) {
    const Rect r = rect.intersect(bounds_);
    if (!r.empty()) {
      dirty_.add(r);
    }
  }

  bool dirty() const { return !dirty_.empty(); }
  int dirtyCount() const { return dirty_.count(); }
  Rect dirtyRect(int index) const { return dirty_.rect(index); }
  void clearDirty() { dirty_.reset(); }

 private:
  uint16_t* framebuffer_ = nullptr;
  int stride_ = 0;
  Rect bounds_{};
  DirtyRegion dirty_;
};

}  // namespace paint
