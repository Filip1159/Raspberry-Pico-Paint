#pragma once

// Low-level drawing primitives (implementation lands in M2).
// All coordinates are clipped to the target rectangle by the caller.

#include <cstdint>

#include "paint/geometry.h"

namespace paint::gfx {

void fillRect(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& rect, uint16_t color);
void hline(uint16_t* fb, int fbWidth, const Rect& clip, int x, int y, int w, uint16_t color);
void vline(uint16_t* fb, int fbWidth, const Rect& clip, int x, int y, int h, uint16_t color);
void blit(uint16_t* fb, int fbWidth, const Rect& clip, Point dst, const uint16_t* src,
          int srcWidth, int srcHeight);
void drawLine(uint16_t* fb, int fbWidth, const Rect& clip, Point a, Point b, uint16_t color);
void drawRect(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& rect, uint16_t color);
void drawCircle(uint16_t* fb, int fbWidth, const Rect& clip, Point center, int radius, uint16_t color);
void fillCircle(uint16_t* fb, int fbWidth, const Rect& clip, Point center, int radius, uint16_t color);
void drawChar(uint16_t* fb, int fbWidth, const Rect& clip, Point pos, char c, uint16_t color, uint8_t scale);
void drawText(uint16_t* fb, int fbWidth, const Rect& clip, Point pos, const char* text, uint16_t color, uint8_t scale);
int textWidth(const char* text, uint8_t scale);

}  // namespace paint::gfx
