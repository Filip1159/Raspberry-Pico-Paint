#pragma once

// Document metadata and save/load orchestration (implementation lands in M6).

#include <cstdint>

namespace paint {

struct DocumentInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t crc = 0;
};

}  // namespace paint
