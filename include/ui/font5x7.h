#pragma once

// 5x7 bitmap font, scaled by an integer factor at draw time.
// The table is column-major: five bytes per glyph, bit 0 is the top row.

#include <cstdint>

namespace paint::font {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kFirstChar = 32;
constexpr int kLastChar = 126;

extern const uint8_t kFont5x7[];

}  // namespace paint::font
