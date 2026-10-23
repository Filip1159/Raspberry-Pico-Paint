#pragma once

// Low-level drawing primitives (implementation lands in M2).
// All coordinates are clipped to the target rectangle by the caller.

#include <cstdint>

#include "paint/geometry.h"

namespace paint::gfx {

void fillRect(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& rect, uint16_t color);
void drawLine(uint16_t* fb, int fbWidth, const Rect& clip, Point a, Point b, uint16_t color);
void drawCircle(uint16_t* fb, int fbWidth, const Rect& clip, Point center, int radius, uint16_t color);

}  // namespace paint::gfx
