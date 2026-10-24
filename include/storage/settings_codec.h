#pragma once

// Binary encoding for /config.bin. Header-only and Arduino-free so the native
// tests can validate the on-flash format without LittleFS.
//
// Layout (little-endian):
//   0  magic        u32  'PCFG'
//   4  version      u16
//   6  payload size u16
//   8  checksum     u32  FNV-1a over the payload
//  12  payload      28 bytes:
//        rotation u8, brightness u8, otaEnabled u8, reserved u8,
//        calibration a,b,c,d,e,f as int32 (a,b,d,e Q16.16; c,f pixels)

#include <cstddef>
#include <cstdint>

#include "storage/settings.h"

namespace paint::settings_codec {

constexpr uint32_t kMagic = 0x47464350u;  // little-endian bytes "PCFG"
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 12;
constexpr size_t kPayloadSize = 28;
constexpr size_t kBlobSize = kHeaderSize + kPayloadSize;

inline uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void writeU16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

inline void writeU32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

inline uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

inline void encode(const Settings& settings, uint8_t* out) {
  if (!out) {
    return;
  }

  uint8_t* payload = out + kHeaderSize;
  payload[0] = settings.rotation;
  payload[1] = settings.brightness;
  payload[2] = settings.otaEnabled ? 1 : 0;
  payload[3] = 0;

  const uint32_t coefficients[6] = {
      static_cast<uint32_t>(settings.calibration.a),
      static_cast<uint32_t>(settings.calibration.b),
      static_cast<uint32_t>(settings.calibration.c),
      static_cast<uint32_t>(settings.calibration.d),
      static_cast<uint32_t>(settings.calibration.e),
      static_cast<uint32_t>(settings.calibration.f),
  };
  for (int i = 0; i < 6; ++i) {
    writeU32(payload + 4 + i * 4, coefficients[i]);
  }

  writeU32(out, kMagic);
  writeU16(out + 4, kVersion);
  writeU16(out + 6, static_cast<uint16_t>(kPayloadSize));
  writeU32(out + 8, checksum(payload, kPayloadSize));
}

inline bool decode(const uint8_t* data, size_t length, Settings& out) {
  if (!data || length != kBlobSize) {
    return false;
  }
  if (readU32(data) != kMagic || readU16(data + 4) != kVersion ||
      readU16(data + 6) != kPayloadSize) {
    return false;
  }

  const uint8_t* payload = data + kHeaderSize;
  if (readU32(data + 8) != checksum(payload, kPayloadSize)) {
    return false;
  }

  Settings decoded;
  decoded.rotation = payload[0];
  decoded.brightness = payload[1];
  decoded.otaEnabled = payload[2] != 0;
  decoded.calibration.a = static_cast<int32_t>(readU32(payload + 4));
  decoded.calibration.b = static_cast<int32_t>(readU32(payload + 8));
  decoded.calibration.c = static_cast<int32_t>(readU32(payload + 12));
  decoded.calibration.d = static_cast<int32_t>(readU32(payload + 16));
  decoded.calibration.e = static_cast<int32_t>(readU32(payload + 20));
  decoded.calibration.f = static_cast<int32_t>(readU32(payload + 24));

  out = decoded;
  return true;
}

}  // namespace paint::settings_codec
