#pragma once

// Precomputed 1-bit round brush masks and gap-free stroke interpolation.
// Header-only and Arduino-free so the native tests exercise the exact code
// that runs on the Pico.

#include <cstdint>
#include <cstring>

#include "paint/canvas.h"
#include "paint/geometry.h"
#include "paint/undo.h"

namespace paint {

struct BrushMask {
  int radius = 0;
  int diameter = 0;
  int stride = 0;                     // bytes per mask row
  const uint8_t* coverage = nullptr;  // 1 bit per pixel, row-major

  bool test(int x, int y) const {
    if (!coverage || x < 0 || y < 0 || x >= diameter || y >= diameter) {
      return false;
    }
    return (coverage[y * stride + (x >> 3)] & (1u << (x & 7))) != 0;
  }
};

namespace brush_detail {

constexpr int kMaskCount = 7;
constexpr int kMaxMaskBytes = 32 * 32 / 8;  // 32x32 bits, one byte per 8 px
constexpr uint8_t kDiameters[kMaskCount] = {1, 2, 4, 8, 16, 24, 32};

struct BrushStorage {
  uint8_t bits[kMaskCount][kMaxMaskBytes] = {};
  BrushMask masks[kMaskCount] = {};
  bool ready = false;
};

inline BrushStorage& storage() {
  static BrushStorage s;
  if (!s.ready) {
    for (int i = 0; i < kMaskCount; ++i) {
      const int d = kDiameters[i];
      const int stride = (d + 7) / 8;
      std::memset(s.bits[i], 0, sizeof(s.bits[i]));

      const float radius = static_cast<float>(d) * 0.5f;
      const float center = (static_cast<float>(d) - 1.0f) * 0.5f;
      const float limit = radius * radius;

      for (int y = 0; y < d; ++y) {
        for (int x = 0; x < d; ++x) {
          const float dx = static_cast<float>(x) - center;
          const float dy = static_cast<float>(y) - center;
          if (dx * dx + dy * dy <= limit) {
            s.bits[i][y * stride + (x >> 3)] |=
                static_cast<uint8_t>(1u << (x & 7));
          }
        }
      }

      s.masks[i].radius = d / 2;
      s.masks[i].diameter = d;
      s.masks[i].stride = stride;
      s.masks[i].coverage = s.bits[i];
    }
    s.ready = true;
  }
  return s;
}

}  // namespace brush_detail

inline const BrushMask& brushMaskForSize(int diameter) {
  const brush_detail::BrushStorage& s = brush_detail::storage();
  int best = 0;
  int bestDistance = 1 << 30;
  for (int i = 0; i < brush_detail::kMaskCount; ++i) {
    const int distance = diameter > brush_detail::kDiameters[i]
                             ? diameter - brush_detail::kDiameters[i]
                             : brush_detail::kDiameters[i] - diameter;
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return s.masks[best];
}

// Write one brush dab. The dab is clipped to the canvas bounds and the
// affected rectangle is added to the canvas dirty region.
inline void stampBrush(Canvas& canvas, const BrushMask& mask, int x, int y,
                       uint16_t color, UndoStack* undo = nullptr) {
  if (!canvas.ready() || !mask.coverage || mask.diameter <= 0) {
    return;
  }

  const int half = mask.diameter / 2;
  const int left = x - half;
  const int top = y - half;
  const Rect dab{static_cast<int16_t>(left), static_cast<int16_t>(top),
                 static_cast<int16_t>(mask.diameter),
                 static_cast<int16_t>(mask.diameter)};
  const Rect clipped = dab.intersect(canvas.bounds());
  if (clipped.empty()) {
    return;
  }

  if (undo) {
    undo->captureRect(canvas, clipped);
  }

  uint16_t* const base = canvas.pixels();
  const int stride = canvas.stride();
  const int firstRow = clipped.y - top;
  const int lastRow = firstRow + clipped.h;
  const int firstCol = clipped.x - left;

  for (int row = firstRow; row < lastRow; ++row) {
    const uint8_t* bits = mask.coverage + row * mask.stride;
    uint16_t* dst = base + (top + row) * stride + clipped.x;
    for (int col = 0; col < clipped.w; ++col) {
      const int maskCol = firstCol + col;
      if ((bits[maskCol >> 3] & (1u << (maskCol & 7))) != 0) {
        dst[col] = color;
      }
    }
  }

  canvas.markDirty(clipped);
}

// Interpolate a stroke. Very large brushes can step a few pixels at a time
// because overlapping dabs still produce a continuous mark.
inline void strokeBrush(Canvas& canvas, const BrushMask& mask, Point a, Point b,
                        uint16_t color, UndoStack* undo = nullptr) {
  int dx = static_cast<int>(b.x) - static_cast<int>(a.x);
  int dy = static_cast<int>(b.y) - static_cast<int>(a.y);
  const int distance = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                           ? (dx < 0 ? -dx : dx)
                           : (dy < 0 ? -dy : dy);
  if (distance == 0) {
    stampBrush(canvas, mask, a.x, a.y, color, undo);
    return;
  }

  int step = mask.diameter / 8;
  if (step < 1) {
    step = 1;
  }
  int steps = distance / step;
  if (steps < 1) {
    steps = 1;
  }

  for (int i = 0; i <= steps; ++i) {
    const int x = static_cast<int>(a.x) + (dx * i) / steps;
    const int y = static_cast<int>(a.y) + (dy * i) / steps;
    stampBrush(canvas, mask, x, y, color, undo);
  }

  // Endpoints are always included even if integer division rounded them away.
  stampBrush(canvas, mask, b.x, b.y, color, undo);
}

}  // namespace paint
