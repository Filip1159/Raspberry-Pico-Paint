#pragma once

// .pnt file format v1 (Appendix A in BACKLOG.md).
//
// Layout (little-endian):
//   0  magic "PNT1"          u32
//   4  version (1)           u16
//   6  flags (0)             u16
//   8  width                 u16
//   10 height                u16
//   12 RLE length            u32
//   16 thumbnail offset      u32
//   20 CRC32 of RLE+thumb    u32
//   24 reserved (0)          u8[8]
//   32 RLE payload
//   .. 64x86 RGB565 thumbnail (raw, 11,008 bytes)
//
// The whole-file encoders/decoders here are Arduino-free so the native tests
// can exercise the format. Streaming saves live in storage/pnt_file.h.

#include <cstddef>
#include <cstdint>

#include "paint/document.h"

namespace paint::pnt {

constexpr uint32_t kMagic = 0x31544E50u;  // little-endian bytes "PNT1"
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 32;
constexpr size_t kReservedSize = 8;

constexpr int kThumbnailWidth = 64;
constexpr int kThumbnailHeight = 86;
constexpr size_t kThumbnailPixels =
    static_cast<size_t>(kThumbnailWidth) * static_cast<size_t>(kThumbnailHeight);
constexpr size_t kThumbnailBytes = kThumbnailPixels * sizeof(uint16_t);

struct Header {
  uint16_t version = kVersion;
  uint16_t flags = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t rleLength = 0;
  uint32_t thumbnailOffset = 0;
  uint32_t crc = 0;
};

// Sink used by the streaming encoder. seek() patches the header after the
// payload has been written; it must support absolute offsets.
struct Sink {
  bool (*write)(void* context, const uint8_t* data, size_t length) = nullptr;
  bool (*seek)(void* context, uint32_t offset) = nullptr;
  void* context = nullptr;
};

void encodeHeader(uint8_t* out, const Header& header);

// Validates magic, version, flags, reserved bytes and the payload layout.
// Only the first kHeaderSize bytes are read.
bool decodeHeader(const uint8_t* data, size_t length, Header& out);

// Nearest-neighbour 64x86 downsample. out must hold kThumbnailPixels.
bool buildThumbnail(const uint16_t* pixels, size_t stride, uint16_t width, uint16_t height,
                    uint16_t* out);

// Copies the raw thumbnail out of a validated .pnt buffer.
bool decodeThumbnail(const uint8_t* data, size_t length, const Header& header, uint16_t* out);

// Encodes pixels (row stride in pixels) into sink. Returns false on invalid
// arguments, short writes or a sink without seek support.
bool encode(const uint16_t* pixels, size_t stride, uint16_t width, uint16_t height,
            const Sink& sink);

// Convenience wrapper for tests: returns bytes written, or 0 on failure.
size_t encodeToBuffer(const uint16_t* pixels, size_t stride, uint16_t width, uint16_t height,
                      uint8_t* dst, size_t dstCapacity);

// Verifies the header, total length and CRC32, then decodes into a contiguous
// width*height RGB565 buffer.
bool decode(const uint8_t* data, size_t length, uint16_t* pixels, DocumentInfo& info);

}  // namespace paint::pnt
