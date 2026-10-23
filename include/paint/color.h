#pragma once

// RGB565 / RGB666 / RGB888 conversion helpers.
// Header-only and Arduino-free so the same code runs on the Pico and in native
// unit tests.

#include <cstdint>

namespace paint {

// Build an RGB565 value from 8-bit components.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

constexpr uint8_t red5(uint16_t c) { return static_cast<uint8_t>((c >> 11) & 0x1F); }
constexpr uint8_t green6(uint16_t c) { return static_cast<uint8_t>((c >> 5) & 0x3F); }
constexpr uint8_t blue5(uint16_t c) { return static_cast<uint8_t>(c & 0x1F); }

// 5/6-bit channel -> 8-bit channel (bit replication, not simple shift).
constexpr uint8_t expand5(uint8_t v) {
  return static_cast<uint8_t>(((v & 0x1F) << 3) | ((v & 0x1F) >> 2));
}
constexpr uint8_t expand6(uint8_t v) {
  return static_cast<uint8_t>(((v & 0x3F) << 2) | ((v & 0x3F) >> 4));
}

// 5/6-bit channel -> 6-bit channel for ILI9488 RGB666 transfers.
constexpr uint8_t red6(uint16_t c) {
  const uint8_t v = red5(c);
  return static_cast<uint8_t>((v << 1) | (v >> 4));
}
constexpr uint8_t green6_from_565(uint16_t c) {
  return green6(c);  // already 6-bit
}
constexpr uint8_t blue6(uint16_t c) {
  const uint8_t v = blue5(c);
  return static_cast<uint8_t>((v << 1) | (v >> 4));
}

constexpr uint8_t red8(uint16_t c) { return expand5(red5(c)); }
constexpr uint8_t green8(uint16_t c) { return expand6(green6(c)); }
constexpr uint8_t blue8(uint16_t c) { return expand5(blue5(c)); }

constexpr uint32_t rgb565_to_rgb888(uint16_t c) {
  return (static_cast<uint32_t>(red8(c)) << 16) |
         (static_cast<uint32_t>(green8(c)) << 8) |
         static_cast<uint32_t>(blue8(c));
}

constexpr uint16_t rgb888_to_rgb565(uint32_t rgb) {
  return rgb565(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                static_cast<uint8_t>((rgb >> 8) & 0xFF),
                static_cast<uint8_t>(rgb & 0xFF));
}

// Common palette values.
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kBlue = 0x001F;
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kCyan = 0x07FF;
constexpr uint16_t kMagenta = 0xF81F;

}  // namespace paint
