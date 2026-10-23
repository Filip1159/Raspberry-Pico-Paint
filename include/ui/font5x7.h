#pragma once

// 5x7 bitmap font, scaled by an integer factor at draw time.
// Glyph data lands with the UI in M3.

#include <cstdint>

namespace paint::font {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kFirstChar = 32;
constexpr int kLastChar = 126;

}  // namespace paint::font
