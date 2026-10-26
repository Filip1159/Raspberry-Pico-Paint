#include "storage/settings.h"

#include <LittleFS.h>

#include "storage/settings_codec.h"

namespace paint {
namespace {

constexpr const char* kConfigPath = "/config.bin";
constexpr const char* kTempPath = "/config.tmp";

}  // namespace

bool loadSettings(Settings& out) {
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) {
    return false;
  }

  uint8_t blob[settings_codec::kBlobSize] = {};
  const size_t expected = file.size();
  const size_t read = file.read(blob, sizeof(blob));
  file.close();

  if (read != expected || read < settings_codec::kHeaderSize) {
    return false;
  }
  return settings_codec::decode(blob, read, out);
}

bool saveSettings(const Settings& settings) {
  uint8_t blob[settings_codec::kBlobSize] = {};
  settings_codec::encode(settings, blob);

  File file = LittleFS.open(kTempPath, "w");
  if (!file) {
    return false;
  }
  const size_t written = file.write(blob, sizeof(blob));
  file.close();

  if (written != sizeof(blob)) {
    LittleFS.remove(kTempPath);
    return false;
  }

  // LittleFS rename replaces an existing destination atomically. Keep a
  // remove+rename fallback for older core versions that reject it.
  if (!LittleFS.rename(kTempPath, kConfigPath)) {
    LittleFS.remove(kConfigPath);
    if (!LittleFS.rename(kTempPath, kConfigPath)) {
      LittleFS.remove(kTempPath);
      return false;
    }
  }
  return true;
}

}  // namespace paint
