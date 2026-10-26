#pragma once

// Document metadata returned by the .pnt loader (format implemented in M6-2).

#include <cstdint>

namespace paint {

struct DocumentInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t crc = 0;
};

}  // namespace paint
