#pragma once

// Line, rectangle and ellipse outlines, scanline flood fill, and an XOR
// rubber-band preview for dragged shapes. Header-only and Arduino-free so the
// native tests exercise the same code that runs on the Pico.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "paint/canvas.h"
#include "paint/geometry.h"
#include "paint/undo.h"

namespace paint {

enum class ShapeKind : uint8_t { Line, Rect, Ellipse };

namespace shape_detail {

inline Point pt(int x, int y) {
  return Point{static_cast<int16_t>(x), static_cast<int16_t>(y)};
}

template <typename Plot>
void rasterLine(Point a, Point b, Plot&& plot) {
  int x = a.x;
  int y = a.y;
  const int dx = std::abs(b.x - a.x);
  const int dy = -std::abs(b.y - a.y);
  const int sx = a.x < b.x ? 1 : -1;
  const int sy = a.y < b.y ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    plot(x, y);
    if (x == b.x && y == b.y) {
      return;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

template <typename Plot>
void rasterRect(Point a, Point b, Plot&& plot) {
  const int x0 = a.x < b.x ? a.x : b.x;
  const int x1 = a.x < b.x ? b.x : a.x;
  const int y0 = a.y < b.y ? a.y : b.y;
  const int y1 = a.y < b.y ? b.y : a.y;
  rasterLine(pt(x0, y0), pt(x1, y0), plot);
  rasterLine(pt(x0, y1), pt(x1, y1), plot);
  rasterLine(pt(x0, y0), pt(x0, y1), plot);
  rasterLine(pt(x1, y0), pt(x1, y1), plot);
}

// Midpoint ellipse inscribed in the box spanned by a and b. Tail pixels can
// fall one pixel outside the box, which the caller's bounds check handles.
template <typename Plot>
void rasterEllipse(Point a, Point b, Plot&& plot) {
  long x0 = a.x < b.x ? a.x : b.x;
  long x1 = a.x < b.x ? b.x : a.x;
  long y0 = a.y < b.y ? a.y : b.y;
  long y1 = a.y < b.y ? b.y : a.y;
  const auto put = [&plot](long x, long y) {
    plot(static_cast<int>(x), static_cast<int>(y));
  };

  long w = x1 - x0;
  long h = y1 - y0;
  long b1 = h & 1;
  long dx = 4 * (1 - w) * h * h;
  long dy = 4 * (b1 + 1) * w * w;
  long err = dx + dy + b1 * w * w;
  long e2;

  y0 += (h + 1) / 2;
  y1 = y0 - b1;
  w *= 8 * w;
  b1 = 8 * h * h;

  do {
    put(x1, y0);
    put(x0, y0);
    put(x0, y1);
    put(x1, y1);
    e2 = 2 * err;
    if (e2 <= dy) {
      y0++;
      y1--;
      err += dy += w;
    }
    if (e2 >= dx || 2 * err > dy) {
      x0++;
      x1--;
      err += dx += b1;
    }
  } while (x0 <= x1);

  while (y0 - y1 < h) {
    put(x0 - 1, y0);
    put(x1 + 1, y0++);
    put(x0 - 1, y1);
    put(x1 + 1, y1--);
  }
}

template <typename Plot>
void rasterShape(ShapeKind kind, Point a, Point b, Plot&& plot) {
  switch (kind) {
    case ShapeKind::Line:
      rasterLine(a, b, plot);
      break;
    case ShapeKind::Rect:
      rasterRect(a, b, plot);
      break;
    case ShapeKind::Ellipse:
      rasterEllipse(a, b, plot);
      break;
  }
}

// Rasterise a shape as horizontal spans rather than individual pixels. Filled
// rectangles and ellipses use one span per row; a line degenerates to 1 px
// spans. Spans keep flood-fill-style undo capture efficient because whole rows
// share the same tile set.
template <typename Span>
void rasterFilledSpans(ShapeKind kind, Point a, Point b, Span&& span) {
  const int x0 = a.x < b.x ? a.x : b.x;
  const int x1 = a.x < b.x ? b.x : a.x;
  const int y0 = a.y < b.y ? a.y : b.y;
  const int y1 = a.y < b.y ? b.y : a.y;

  if (kind == ShapeKind::Line) {
    rasterLine(a, b, [&](int x, int y) { span(x, y, 1); });
    return;
  }
  if (kind == ShapeKind::Rect) {
    for (int y = y0; y <= y1; ++y) {
      span(x0, y, x1 - x0 + 1);
    }
    return;
  }

  const float cx = (x0 + x1) * 0.5f;
  const float cy = (y0 + y1) * 0.5f;
  const float rx = (x1 - x0) * 0.5f;
  const float ry = (y1 - y0) * 0.5f;
  if (rx < 0.5f && ry < 0.5f) {
    span(x0, y0, 1);
    return;
  }
  if (ry < 0.5f) {
    for (int y = y0; y <= y1; ++y) {
      span(x0, y, x1 - x0 + 1);
    }
    return;
  }

  for (int y = y0; y <= y1; ++y) {
    const float t = (static_cast<float>(y) - cy) / ry;
    if (t < -1.0f || t > 1.0f) {
      continue;
    }
    const float inside = 1.0f - t * t;
    const float dx = inside <= 0.0f ? 0.0f : rx * std::sqrt(inside);
    int left = static_cast<int>(std::ceil(cx - dx - 0.001f));
    int right = static_cast<int>(std::floor(cx + dx + 0.001f));
    if (left < x0) {
      left = x0;
    }
    if (right > x1) {
      right = x1;
    }
    if (right >= left) {
      span(left, y, right - left + 1);
    }
  }
}

// Bounding box of a shape, padded by one pixel for outline and tail pixels.
inline Rect shapeBounds(Point a, Point b) {
  const int x0 = a.x < b.x ? a.x : b.x;
  const int x1 = a.x < b.x ? b.x : a.x;
  const int y0 = a.y < b.y ? a.y : b.y;
  const int y1 = a.y < b.y ? b.y : a.y;
  return Rect{static_cast<int16_t>(x0 - 1), static_cast<int16_t>(y0 - 1),
              static_cast<int16_t>(x1 - x0 + 3), static_cast<int16_t>(y1 - y0 + 3)};
}

}  // namespace shape_detail

// Rubber-band preview. Saves the background pixels under the shape path, draws
// the shape in the actual colour, and on update/cancel restores the saved
// background. No XOR inversion — the preview shows the real colour from the
// start.
//
// Filled shapes preview as an outline while dragging. Saving a full filled
// region would need a second framebuffer; the outline still communicates the
// final bounds, and commit fills the shape with one undoable action.
class ShapePreview {
 public:
  static constexpr int kMaxPixels = 2048;  // max outline perimeter (2*(320+480))

  struct SavedPixel {
    int16_t x;
    int16_t y;
    uint16_t color;
  };

  bool active() const { return active_; }

  void update(Canvas& canvas, ShapeKind kind, Point a, Point b, uint16_t color,
              bool filled = false) {
    (void)filled;  // outline preview is used for both modes
    if (!canvas.ready()) {
      return;
    }
    // Restore previous background.
    if (active_) {
      restore(canvas);
    }

    // Compute new bounds and save new background.
    kind_ = kind;
    a_ = a;
    b_ = b;
    color_ = color;

    count_ = 0;
    shape_detail::rasterShape(kind_, a_, b_, [&](int x, int y) {
      if (!inBounds(canvas, x, y) || count_ >= kMaxPixels) {
        return;
      }
      saved_[count_++] = {static_cast<int16_t>(x), static_cast<int16_t>(y),
                          canvas.pixels()[y * canvas.stride() + x]};
    });

    // Draw new shape in the actual colour.
    shape_detail::rasterShape(kind_, a_, b_, [&](int x, int y) {
      if (!inBounds(canvas, x, y)) {
        return;
      }
      canvas.pixels()[y * canvas.stride() + x] = color_;
    });

    canvas.markDirty(shape_detail::shapeBounds(a_, b_));
    active_ = true;
  }

  void cancel(Canvas& canvas) {
    if (!active_) {
      return;
    }
    restore(canvas);
    canvas.markDirty(shape_detail::shapeBounds(a_, b_));
    active_ = false;
  }

  // Replaces any preview with the final shape and records it as one undo
  // action. The caller publishes the action with undo.commit().
  void commit(Canvas& canvas, UndoStack& undo, ShapeKind kind, Point a, Point b,
              uint16_t color, bool filled = false) {
    cancel(canvas);
    undo.beginAction();
    if (filled) {
      shape_detail::rasterFilledSpans(
          kind, a, b, [&](int x, int y, int width) {
            const Rect span = Rect{static_cast<int16_t>(x),
                                   static_cast<int16_t>(y),
                                   static_cast<int16_t>(width), 1}
                                  .intersect(canvas.bounds());
            if (span.empty()) {
              return;
            }
            undo.captureRect(canvas, span);
            canvas.fillRect(span, color);
          });
    } else {
      shape_detail::rasterShape(kind, a, b, [&](int x, int y) {
        if (!inBounds(canvas, x, y)) {
          return;
        }
        undo.captureRect(
            canvas,
            Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), 1, 1});
        canvas.pixels()[y * canvas.stride() + x] = color;
      });
    }
    canvas.markDirty(shape_detail::shapeBounds(a, b));
  }

 private:
  static bool inBounds(const Canvas& canvas, int x, int y) {
    return canvas.bounds().contains(static_cast<int16_t>(x), static_cast<int16_t>(y));
  }

  void restore(Canvas& canvas) {
    for (int i = 0; i < count_; ++i) {
      const SavedPixel& p = saved_[i];
      canvas.pixels()[p.y * canvas.stride() + p.x] = p.color;
    }
    count_ = 0;
  }

  SavedPixel saved_[kMaxPixels];
  int count_ = 0;
  ShapeKind kind_ = ShapeKind::Line;
  Point a_{};
  Point b_{};
  uint16_t color_ = 0;
  bool active_ = false;
};

// Scanline flood fill with exact RGB565 matching, clipped to the canvas. Every
// span is captured in undo before it is written when undo is non-null. Returns
// false if the seed stack overflowed; the part already filled stays painted.
constexpr int kFloodStackSize = 1024;

inline bool floodFill(Canvas& canvas, UndoStack* undo, Point seed, uint16_t color) {
  if (!canvas.ready() || !canvas.bounds().contains(seed)) {
    return false;
  }
  const uint16_t target = canvas.pixel(seed.x, seed.y);
  if (target == color) {
    return true;
  }

  static Point stack[kFloodStackSize];
  const Rect bounds = canvas.bounds();
  int top = 0;
  stack[top++] = seed;

  while (top > 0) {
    const Point p = stack[--top];
    if (canvas.pixel(p.x, p.y) != target) {
      continue;
    }

    int left = p.x;
    while (left > bounds.x && canvas.pixel(left - 1, p.y) == target) {
      --left;
    }
    int right = p.x;
    while (right < bounds.right() - 1 && canvas.pixel(right + 1, p.y) == target) {
      ++right;
    }

    const Rect span{static_cast<int16_t>(left), p.y,
                    static_cast<int16_t>(right - left + 1), 1};
    if (undo) {
      undo->captureRect(canvas, span);
    }
    canvas.fillRect(span, color);

    for (int side = 0; side < 2; ++side) {
      const int ny = p.y + (side == 0 ? -1 : 1);
      if (ny < bounds.y || ny >= bounds.bottom()) {
        continue;
      }
      bool inRun = false;
      for (int x = left; x <= right; ++x) {
        const bool match = canvas.pixel(x, ny) == target;
        if (match && !inRun) {
          if (top >= kFloodStackSize) {
            return false;
          }
          stack[top++] = shape_detail::pt(x, ny);
        }
        inRun = match;
      }
    }
  }
  return true;
}

}  // namespace paint
