#pragma once

// LittleFS wrapper, gallery index, atomic writes (implementation lands in M6).

#include <cstddef>
#include <cstdint>

namespace paint {

struct StorageStats {
  size_t totalBytes = 0;
  size_t usedBytes = 0;
  size_t freeBytes = 0;
};

class Storage {
 public:
  bool begin();
  StorageStats stats() const;
  bool format();
};

}  // namespace paint
