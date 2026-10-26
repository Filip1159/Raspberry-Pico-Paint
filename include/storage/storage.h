#pragma once

// LittleFS stats/format helper. The gallery index lives in paint_files.h and
// the atomic .pnt path in pnt_file.h.
//
// The 700 KB reserve is for OTA staging. Gallery drawings may use the rest of
// the filesystem, but starting an update below this threshold is refused.

#include <cstddef>
#include <cstdint>

namespace paint {

constexpr size_t kOtaHeadroomBytes = 700u * 1024u;

struct StorageStats {
  size_t totalBytes = 0;
  size_t usedBytes = 0;
  size_t freeBytes = 0;

  uint8_t usedPercent() const {
    if (totalBytes == 0) {
      return 0;
    }
    const uint64_t percent =
        (static_cast<uint64_t>(usedBytes) * 100u) / totalBytes;
    return percent > 100 ? 100 : static_cast<uint8_t>(percent);
  }

  bool otaHeadroom() const {
    return totalBytes != 0 && freeBytes >= kOtaHeadroomBytes;
  }
};

inline StorageStats makeStorageStats(size_t totalBytes, size_t usedBytes) {
  StorageStats stats;
  stats.totalBytes = totalBytes;
  stats.usedBytes = usedBytes > totalBytes ? totalBytes : usedBytes;
  stats.freeBytes = totalBytes > usedBytes ? totalBytes - usedBytes : 0;
  return stats;
}

class Storage {
 public:
  bool begin();
  StorageStats stats() const;
  bool format();
};

}  // namespace paint
