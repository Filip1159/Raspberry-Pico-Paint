#pragma once

// 5x7 bitmap font, scaled by an integer factor at draw time.
// The table is column-major: five bytes per glyph, bit 0 is the top row.

#include <cstdint>

namespace paint::font {

constexpr int kGlyphWidth = 5;
// Each glyph has eight column bits: seven visible rows plus a descender row.
constexpr int kGlyphHeight = 8;
// The table includes all 256 byte values and starts at ASCII 0. Keeping the
// full range also lets the UI show the degree sign and other extended glyphs.
constexpr int kFirstChar = 0;
constexpr int kLastChar = 255;

extern const uint8_t kFont5x7[];

}  // namespace paint::font
