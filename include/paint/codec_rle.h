#pragma once

// RLE codec for .pnt files (implementation lands in M6).

#include <cstddef>
#include <cstdint>

namespace paint {

// Encodes count RGB565 pixels into dst. Returns bytes written, or 0 if the
// destination is too small.
size_t rleEncode(const uint16_t* src, size_t count, uint8_t* dst, size_t dstCapacity);

// Decodes exactly count pixels into dst. Returns false on malformed input.
bool rleDecode(const uint8_t* src, size_t srcLength, uint16_t* dst, size_t count);

}  // namespace paint
