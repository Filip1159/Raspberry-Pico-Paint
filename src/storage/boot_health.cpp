#include "storage/boot_health.h"

#include <LittleFS.h>

namespace paint::boot_health {
namespace {

constexpr const char* kPath = "/boot.health";
constexpr const char* kTempPath = "/boot.tmp";

bool store(const State& state) {
  uint8_t blob[kBlobSize] = {};
  encode(state, blob);

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

  if (!LittleFS.rename(kTempPath, kPath)) {
    LittleFS.remove(kPath);
    if (!LittleFS.rename(kTempPath, kPath)) {
      LittleFS.remove(kTempPath);
      return false;
    }
  }
  return true;
}

}  // namespace

State load() {
  File file = LittleFS.open(kPath, "r");
  if (!file) {
    return State{};
  }

  uint8_t blob[kBlobSize] = {};
  const size_t expected = file.size();
  const size_t read = file.read(blob, sizeof(blob));
  file.close();

  State state;
  if (read != expected || !decode(blob, read, state)) {
    return State{};
  }
  return state;
}

State begin() {
  State state = load();
  if (!state.valid) {
    state = State{0, true};
  }
  if (state.badBoots < config::kBootFailThreshold) {
    ++state.badBoots;
  }
  // Keep the in-memory count even if the write fails (read-only/full
  // filesystem): this boot still enters recovery, and the user can fix storage
  // from there.
  store(state);
  return state;
}

bool clear() {
  State state;
  state.badBoots = 0;
  state.valid = true;
  return store(state);
}

}  // namespace paint::boot_health
