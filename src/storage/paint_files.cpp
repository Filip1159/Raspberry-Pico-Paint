#include "storage/paint_files.h"

#include <LittleFS.h>

#include <cstdio>
#include <cstring>

namespace paint {
namespace {

bool ensurePaintDirectory() {
  if (LittleFS.exists(kPaintDirectory)) {
    return true;
  }
  return LittleFS.mkdir(kPaintDirectory);
}

void sortNames(char names[][kPaintFileNameSize], int count) {
  for (int i = 1; i < count; ++i) {
    char value[kPaintFileNameSize];
    std::memcpy(value, names[i], sizeof(value));
    int j = i - 1;
    while (j >= 0 && std::strcmp(names[j], value) > 0) {
      std::memcpy(names[j + 1], names[j], sizeof(value));
      --j;
    }
    std::memcpy(names[j + 1], value, sizeof(value));
  }
}

}  // namespace

int PaintFileIndex::refresh() {
  count_ = 0;
  totalCount_ = 0;
  if (!ensurePaintDirectory()) {
    return 0;
  }

  File dir = LittleFS.open(kPaintDirectory, "r");
  if (!dir || !dir.isDirectory()) {
    return 0;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char* name = entry.name();
      if (isPntFileName(name) && !isAutosaveFileName(name) &&
          std::strlen(name) < kPaintFileNameSize) {
        ++totalCount_;
        if (count_ < kMaxPaintFiles) {
          std::snprintf(names_[count_], kPaintFileNameSize, "%s", name);
          ++count_;
        }
      }
    }
    entry = dir.openNextFile();
  }
  dir.close();

  sortNames(names_, count_);
  return count_;
}

const char* PaintFileIndex::name(int index) const {
  if (index < 0 || index >= count_) {
    return "";
  }
  return names_[index];
}

bool PaintFileIndex::remove(int index) {
  char path[32];
  if (index < 0 || index >= count_ ||
      !paintFilePath(names_[index], path, sizeof(path))) {
    return false;
  }

  if (!LittleFS.remove(path)) {
    return false;
  }

  for (int i = index; i + 1 < count_; ++i) {
    std::memcpy(names_[i], names_[i + 1], kPaintFileNameSize);
  }
  --count_;
  if (totalCount_ > 0) {
    --totalCount_;
  }
  return true;
}

bool PaintFileIndex::nextName(char* out, size_t capacity) const {
  if (!out || capacity == 0 || !ensurePaintDirectory()) {
    return false;
  }

  char name[kPaintFileNameSize];
  char path[32];
  for (int number = 1; number <= 9999; ++number) {
    if (!paintFileNameForNumber(number, name, sizeof(name)) ||
        !paintFilePath(name, path, sizeof(path))) {
      return false;
    }
    if (!LittleFS.exists(path)) {
      std::snprintf(out, capacity, "%s", name);
      return true;
    }
  }
  return false;
}

}  // namespace paint
