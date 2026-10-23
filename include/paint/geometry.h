#pragma once

// Minimal integer geometry types. Header-only and Arduino-free.

#include <cstdint>

namespace paint {

struct Point {
  int16_t x = 0;
  int16_t y = 0;
};

struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;

  constexpr bool empty() const { return w <= 0 || h <= 0; }
  constexpr int16_t right() const { return static_cast<int16_t>(x + w); }
  constexpr int16_t bottom() const { return static_cast<int16_t>(y + h); }

  constexpr bool contains(int16_t px, int16_t py) const {
    return !empty() && px >= x && py >= y && px < right() && py < bottom();
  }
  constexpr bool contains(Point p) const { return contains(p.x, p.y); }

  constexpr Rect intersect(const Rect& other) const {
    const int16_t nx = x > other.x ? x : other.x;
    const int16_t ny = y > other.y ? y : other.y;
    const int16_t nr = right() < other.right() ? right() : other.right();
    const int16_t nb = bottom() < other.bottom() ? bottom() : other.bottom();
    return Rect{nx, ny, static_cast<int16_t>(nr - nx), static_cast<int16_t>(nb - ny)};
  }

  constexpr Rect unite(const Rect& other) const {
    if (empty()) {
      return other;
    }
    if (other.empty()) {
      return *this;
    }

    const int16_t nx = x < other.x ? x : other.x;
    const int16_t ny = y < other.y ? y : other.y;
    const int16_t nr = right() > other.right() ? right() : other.right();
    const int16_t nb = bottom() > other.bottom() ? bottom() : other.bottom();
    return Rect{nx, ny, static_cast<int16_t>(nr - nx), static_cast<int16_t>(nb - ny)};
  }

  constexpr bool operator==(const Rect& other) const {
    return x == other.x && y == other.y && w == other.w && h == other.h;
  }
};

constexpr Rect makeRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  return Rect{x, y, w, h};
}

}  // namespace paint
