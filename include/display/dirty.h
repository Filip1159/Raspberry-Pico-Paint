#pragma once

// Accumulates dirty rectangles and merges them into one bounding box.
// Canvas and UI code add damaged regions, then the renderer flushes once.

#include "paint/geometry.h"

namespace paint {

class DirtyRegion {
 public:
  void add(const Rect& rect) {
    if (rect.empty()) {
      return;
    }
    if (empty_) {
      bounds_ = rect;
      empty_ = false;
    } else {
      bounds_ = bounds_.unite(rect);
    }
  }

  bool empty() const { return empty_; }
  Rect bounds() const { return empty_ ? Rect{} : bounds_; }

  Rect take() {
    const Rect result = bounds();
    reset();
    return result;
  }

  void reset() {
    bounds_ = Rect{};
    empty_ = true;
  }

 private:
  Rect bounds_{};
  bool empty_ = true;
};

}  // namespace paint
