#include "storage/storage.h"

#include <LittleFS.h>

namespace paint {

bool Storage::begin() {
  return LittleFS.begin();
}

StorageStats Storage::stats() const {
  FSInfo info;
  if (!LittleFS.info(info)) {
    return StorageStats{};
  }
  return makeStorageStats(static_cast<size_t>(info.totalBytes),
                          static_cast<size_t>(info.usedBytes));
}

bool Storage::format() {
  return LittleFS.format();
}

}  // namespace paint
