#pragma once

// Boot-health counter (M8-3). A tiny /boot.health file counts consecutive
// boots that did not survive long enough to be marked healthy. The main loop
// clears it after five seconds; if the count reaches three, the next boot
// enters recovery mode without Wi-Fi or autosave.
//
// Encoding is header-only and Arduino-free; the LittleFS access lives in
// src/storage/boot_health.cpp.

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "paint/crc32.h"

namespace paint::boot_health {

constexpr size_t kBlobSize = 8;
constexpr uint8_t kVersion = 1;

struct State {
  uint8_t badBoots = 0;
  bool valid = false;

  bool recoveryRequired() const {
    return valid && badBoots >= config::kBootFailThreshold;
  }
};

namespace detail {

inline void writeU32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

inline uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace detail

// Blob layout: 'P','B', version, badBoots, CRC32 of the first four bytes.
inline void encode(const State& state, uint8_t out[kBlobSize]) {
  if (!out) {
    return;
  }
  out[0] = 'P';
  out[1] = 'B';
  out[2] = kVersion;
  out[3] = state.badBoots > config::kBootFailThreshold
               ? config::kBootFailThreshold
               : state.badBoots;
  detail::writeU32(out + 4, crc32(out, 4));
}

inline bool decode(const uint8_t* data, size_t length, State& out) {
  if (!data || length != kBlobSize || data[0] != 'P' || data[1] != 'B' ||
      data[2] != kVersion) {
    return false;
  }
  if (detail::readU32(data + 4) != crc32(data, 4)) {
    return false;
  }

  State decoded;
  decoded.badBoots = data[3] > config::kBootFailThreshold
                         ? config::kBootFailThreshold
                         : data[3];
  decoded.valid = true;
  out = decoded;
  return true;
}

// Load the persisted state without changing it.
State load();

// Read the current count, increment it for this boot (clamped to the
// threshold), persist the result, and return it.
State begin();

// Mark the current boot healthy by writing a zero count. This is called after
// kBootHealthyMs and when the user continues/reboots from recovery mode.
bool clear();

}  // namespace paint::boot_health
