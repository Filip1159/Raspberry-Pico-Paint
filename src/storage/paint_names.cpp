#include "storage/paint_files.h"

#include <cstdio>
#include <cstring>

namespace paint {
namespace {

bool hasPntSuffix(const char* name) {
  const size_t length = std::strlen(name);
  return length >= 5 && std::strcmp(name + length - 4, ".pnt") == 0;
}

}  // namespace

bool isPntFileName(const char* name) {
  return name && *name && !std::strchr(name, '/') && hasPntSuffix(name);
}

bool isAutosaveFileName(const char* name) {
  return name && std::strcmp(name, kAutosaveFileName) == 0;
}

int paintFileNumber(const char* name) {
  if (!name || std::strlen(name) != 11 || std::strncmp(name, "img", 3) != 0 ||
      std::strcmp(name + 7, ".pnt") != 0) {
    return -1;
  }

  int number = 0;
  for (int i = 3; i < 7; ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return -1;
    }
    number = number * 10 + (name[i] - '0');
  }
  return number > 0 ? number : -1;
}

bool paintFileNameForNumber(int number, char* out, size_t capacity) {
  if (!out || number < 1 || number > 9999) {
    return false;
  }
  const int written = std::snprintf(out, capacity, "img%04d.pnt", number);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool paintFilePath(const char* name, char* out, size_t capacity) {
  if (!name || !*name || std::strchr(name, '/') || !out || capacity == 0) {
    return false;
  }
  const int written = std::snprintf(out, capacity, "%s/%s", kPaintDirectory, name);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

}  // namespace paint
