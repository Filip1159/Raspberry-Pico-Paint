#include "paint/codec_rle.h"

namespace paint {
namespace {

constexpr uint16_t kRunFlag = 0x8000u;
constexpr size_t kMaxTokenLength = 0x7FFFu;

uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

bool putU16(uint8_t* dst, size_t capacity, size_t& offset, uint16_t value) {
  if (offset + 2 > capacity) {
    return false;
  }
  dst[offset++] = static_cast<uint8_t>(value & 0xFF);
  dst[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
  return true;
}

// Number of identical pixels starting at index, capped at the token limit.
size_t runLength(const uint16_t* src, size_t index, size_t count) {
  size_t run = 1;
  while (index + run < count && run < kMaxTokenLength && src[index + run] == src[index]) {
    ++run;
  }
  return run;
}

bool decodeInternal(const uint8_t* src, size_t srcLength, uint16_t* dst, size_t count) {
  if (!src && srcLength) {
    return false;
  }

  size_t offset = 0;
  size_t written = 0;
  while (written < count) {
    if (offset + 2 > srcLength) {
      return false;
    }

    const uint16_t token = readU16(src + offset);
    offset += 2;

    if (token & kRunFlag) {
      const size_t length = token & 0x7FFF;
      if (length == 0 || length > count - written || offset + 2 > srcLength) {
        return false;
      }
      const uint16_t value = readU16(src + offset);
      offset += 2;
      if (dst) {
        for (size_t i = 0; i < length; ++i) {
          dst[written++] = value;
        }
      } else {
        written += length;
      }
    } else {
      const size_t length = token;
      if (length == 0 || length > count - written || offset + length * 2 > srcLength) {
        return false;
      }
      if (dst) {
        for (size_t i = 0; i < length; ++i) {
          dst[written++] = readU16(src + offset);
          offset += 2;
        }
      } else {
        offset += length * 2;
        written += length;
      }
    }
  }

  return offset == srcLength;
}

}  // namespace

size_t rleEncode(const uint16_t* src, size_t count, uint8_t* dst, size_t dstCapacity) {
  if ((!src && count) || !dst) {
    return 0;
  }

  size_t offset = 0;
  size_t index = 0;
  while (index < count) {
    const size_t run = runLength(src, index, count);

    // A run costs four bytes; two literal pixels also cost six bytes with the
    // token, so two equal pixels are already worth encoding as a run.
    if (run >= 2) {
      if (!putU16(dst, dstCapacity, offset, static_cast<uint16_t>(kRunFlag | run)) ||
          !putU16(dst, dstCapacity, offset, src[index])) {
        return 0;
      }
      index += run;
      continue;
    }

    const size_t start = index;
    size_t length = 0;
    while (index < count && length < kMaxTokenLength) {
      if (runLength(src, index, count) >= 2) {
        break;
      }
      ++index;
      ++length;
    }

    if (!putU16(dst, dstCapacity, offset, static_cast<uint16_t>(length))) {
      return 0;
    }
    for (size_t i = 0; i < length; ++i) {
      if (!putU16(dst, dstCapacity, offset, src[start + i])) {
        return 0;
      }
    }
  }

  return offset;
}

bool rleDecode(const uint8_t* src, size_t srcLength, uint16_t* dst, size_t count) {
  if (!dst && count) {
    return false;
  }
  return decodeInternal(src, srcLength, dst, count);
}

bool rleValidate(const uint8_t* src, size_t srcLength, size_t count) {
  return decodeInternal(src, srcLength, nullptr, count);
}

}  // namespace paint
