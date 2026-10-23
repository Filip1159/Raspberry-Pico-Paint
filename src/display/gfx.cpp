#include "display/gfx.h"

#include <cstdlib>
#include <cstring>

#include "ui/font5x7.h"

namespace paint::gfx {
namespace {

void plot(uint16_t* fb, int fbWidth, const Rect& clip, int x, int y, uint16_t color) {
  if (clip.contains(static_cast<int16_t>(x), static_cast<int16_t>(y))) {
    fb[y * fbWidth + x] = color;
  }
}

}  // namespace

void fillRect(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& rect, uint16_t color) {
  const Rect r = rect.intersect(clip);
  if (r.empty()) {
    return;
  }

  for (int y = r.y; y < r.bottom(); ++y) {
    uint16_t* row = fb + y * fbWidth + r.x;
    for (int x = 0; x < r.w; ++x) {
      row[x] = color;
    }
  }
}

void hline(uint16_t* fb, int fbWidth, const Rect& clip, int x, int y, int w, uint16_t color) {
  fillRect(fb, fbWidth, clip,
           {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), 1}, color);
}

void vline(uint16_t* fb, int fbWidth, const Rect& clip, int x, int y, int h, uint16_t color) {
  fillRect(fb, fbWidth, clip,
           {static_cast<int16_t>(x), static_cast<int16_t>(y), 1, static_cast<int16_t>(h)}, color);
}

void blit(uint16_t* fb, int fbWidth, const Rect& clip, Point dst, const uint16_t* src,
          int srcWidth, int srcHeight) {
  if (!src || srcWidth <= 0 || srcHeight <= 0) {
    return;
  }

  const Rect dest = {dst.x, dst.y, static_cast<int16_t>(srcWidth), static_cast<int16_t>(srcHeight)};
  const Rect r = dest.intersect(clip);
  if (r.empty()) {
    return;
  }

  const int sx = r.x - dst.x;
  const int sy = r.y - dst.y;
  for (int y = 0; y < r.h; ++y) {
    const uint16_t* srcRow = src + static_cast<size_t>(sy + y) * srcWidth + sx;
    uint16_t* dstRow = fb + static_cast<size_t>(r.y + y) * fbWidth + r.x;
    std::memcpy(dstRow, srcRow, static_cast<size_t>(r.w) * sizeof(uint16_t));
  }
}

void drawLine(uint16_t* fb, int fbWidth, const Rect& clip, Point a, Point b, uint16_t color) {
  int x0 = a.x;
  int y0 = a.y;
  const int x1 = b.x;
  const int y1 = b.y;

  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    plot(fb, fbWidth, clip, x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void drawRect(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& rect, uint16_t color) {
  if (rect.empty()) {
    return;
  }
  const int16_t x1 = static_cast<int16_t>(rect.x + rect.w - 1);
  const int16_t y1 = static_cast<int16_t>(rect.y + rect.h - 1);

  drawLine(fb, fbWidth, clip, {rect.x, rect.y}, {x1, rect.y}, color);
  drawLine(fb, fbWidth, clip, {rect.x, y1}, {x1, y1}, color);
  drawLine(fb, fbWidth, clip, {rect.x, rect.y}, {rect.x, y1}, color);
  drawLine(fb, fbWidth, clip, {x1, rect.y}, {x1, y1}, color);
}

void drawCircle(uint16_t* fb, int fbWidth, const Rect& clip, Point center, int radius, uint16_t color) {
  if (radius < 0) {
    return;
  }

  int x = radius;
  int y = 0;
  int err = 1 - x;

  while (x >= y) {
    plot(fb, fbWidth, clip, center.x + x, center.y + y, color);
    plot(fb, fbWidth, clip, center.x + y, center.y + x, color);
    plot(fb, fbWidth, clip, center.x - y, center.y + x, color);
    plot(fb, fbWidth, clip, center.x - x, center.y + y, color);
    plot(fb, fbWidth, clip, center.x - x, center.y - y, color);
    plot(fb, fbWidth, clip, center.x - y, center.y - x, color);
    plot(fb, fbWidth, clip, center.x + y, center.y - x, color);
    plot(fb, fbWidth, clip, center.x + x, center.y - y, color);

    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x + 1);
    }
  }
}

void drawChar(uint16_t* fb, int fbWidth, const Rect& clip, Point pos, char c, uint16_t color, uint8_t scale) {
  if (c < font::kFirstChar || c > font::kLastChar || scale == 0) {
    return;
  }

  const uint8_t* glyph = &font::kFont5x7[(c - font::kFirstChar) * font::kGlyphWidth];
  for (int col = 0; col < font::kGlyphWidth; ++col) {
    const uint8_t bits = glyph[col];
    for (int row = 0; row < 8; ++row) {
      if (bits & (1u << row)) {
        fillRect(fb, fbWidth, clip,
                 {static_cast<int16_t>(pos.x + col * scale),
                  static_cast<int16_t>(pos.y + row * scale),
                  static_cast<int16_t>(scale), static_cast<int16_t>(scale)},
                 color);
      }
    }
  }
}

void drawText(uint16_t* fb, int fbWidth, const Rect& clip, Point pos, const char* text, uint16_t color, uint8_t scale) {
  if (!text || scale == 0) {
    return;
  }

  const int16_t startX = pos.x;
  int16_t x = pos.x;
  int16_t y = pos.y;

  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      x = startX;
      y = static_cast<int16_t>(y + 8 * scale);
      continue;
    }
    drawChar(fb, fbWidth, clip, {x, y}, *p, color, scale);
    x = static_cast<int16_t>(x + 6 * scale);
  }
}

int textWidth(const char* text, uint8_t scale) {
  if (!text) {
    return 0;
  }
  return static_cast<int>(std::strlen(text)) * 6 * scale;
}

}  // namespace paint::gfx
