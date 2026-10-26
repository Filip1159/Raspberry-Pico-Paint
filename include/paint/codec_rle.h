#pragma once

// RLE codec for .pnt files. The stream is a sequence of little-endian 16-bit
// tokens:
//   bit 15 set   run     -> length (token & 0x7FFF), followed by one RGB565
//   bit 15 clear literal -> length (token), followed by that many RGB565

#include <cstddef>
#include <cstdint>

namespace paint {

// Encodes count RGB565 pixels into dst. Returns bytes written, or 0 if the
// destination is too small or the arguments are invalid. Empty input succeeds
// with a length of 0.
size_t rleEncode(const uint16_t* src, size_t count, uint8_t* dst, size_t dstCapacity);

// Decodes exactly count pixels into dst. Returns false on malformed input,
// including truncated tokens, zero-length runs, overruns and trailing bytes.
bool rleDecode(const uint8_t* src, size_t srcLength, uint16_t* dst, size_t count);

// Checks that src is a structurally valid stream for exactly count pixels
// without writing any output.
bool rleValidate(const uint8_t* src, size_t srcLength, size_t count);

}  // namespace paint
