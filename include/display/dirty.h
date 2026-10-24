#pragma once

// Accumulates dirty rectangles. Overlapping rectangles are merged; disjoint
// rectangles are kept separate so a UI button and a bottom sheet can be
// flushed independently instead of forcing a full-height bounding box.

#include "paint/geometry.h"

namespace paint {

class DirtyRegion {
 public:
  static constexpr int kMaxRects = 8;

  void add(const Rect& rect) {
    if (rect.empty()) {
      return;
    }

    Rect merged = rect;
    bool changed = true;
    while (changed) {
      changed = false;
      for (int i = 0; i < count_; ++i) {
        if (rects_[i].intersect(merged).empty()) {
          continue;
        }
        merged = rects_[i].unite(merged);
        for (int j = i; j < count_ - 1; ++j) {
          rects_[j] = rects_[j + 1];
        }
        --count_;
        changed = true;
        break;
      }
    }

    if (count_ < kMaxRects) {
      rects_[count_++] = merged;
      return;
    }

    // The list is full: collapse everything into one bounding box. This keeps
    // correctness at the cost of one larger flush in an already busy frame.
    const Rect b = bounds().unite(merged);
    rects_[0] = b;
    count_ = 1;
  }

  bool empty() const { return count_ == 0; }
  int count() const { return count_; }
  Rect rect(int index) const {
    return (index >= 0 && index < count_) ? rects_[index] : Rect{};
  }

  Rect bounds() const {
    Rect result{};
    for (int i = 0; i < count_; ++i) {
      result = result.unite(rects_[i]);
    }
    return result;
  }

  Rect take() {
    const Rect result = bounds();
    reset();
    return result;
  }

  void reset() { count_ = 0; }

 private:
  Rect rects_[kMaxRects] = {};
  int count_ = 0;
};

}  // namespace paint
