#pragma once

// CRC-32 (IEEE 802.3) used by .pnt files. Header-only and Arduino-free so the
// native tests can validate the checksum without pulling in LittleFS.
//
// The running value is not inverted between calls: start at 0, chain updates,
// and the returned uint32_t is the final CRC. This keeps streaming callers
// simple (header, then RLE payload, then thumbnail).

#include <cstddef>
#include <cstdint>

namespace paint {

inline uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t length) {
  uint32_t crc = ~state;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

inline uint32_t crc32(const uint8_t* data, size_t length) {
  return crc32Update(0, data, length);
}

}  // namespace paint
