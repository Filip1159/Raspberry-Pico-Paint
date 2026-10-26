#include "paint/pnt.h"

#include <cstring>

#include "paint/codec_rle.h"
#include "paint/crc32.h"

namespace paint::pnt {
namespace {

constexpr size_t kRleChunkPixels = 512;
constexpr size_t kWriteBufferBytes = 256;

uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void writeU16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeU32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool patchU32(const Sink& sink, uint32_t offset, uint32_t value) {
  uint8_t bytes[4];
  writeU32(bytes, value);
  return sink.seek(sink.context, offset) && sink.write(sink.context, bytes, sizeof(bytes));
}

// Buffers small writes and computes the CRC as the bytes leave for the sink.
class BufferedWriter {
 public:
  BufferedWriter(const Sink& sink, uint8_t* buffer, size_t capacity)
      : sink_(sink), buffer_(buffer), capacity_(capacity) {}

  bool write(const uint8_t* data, size_t length) {
    while (length > 0) {
      if (used_ == capacity_ && !flush()) {
        return false;
      }
      const size_t space = capacity_ - used_;
      const size_t take = length < space ? length : space;
      std::memcpy(buffer_ + used_, data, take);
      used_ += take;
      data += take;
      length -= take;
      bytes_ += take;
    }
    return true;
  }

  bool flush() {
    if (used_ == 0) {
      return true;
    }
    if (!sink_.write(sink_.context, buffer_, used_)) {
      return false;
    }
    crc_ = crc32Update(crc_, buffer_, used_);
    used_ = 0;
    return true;
  }

  size_t bytes() const { return bytes_; }
  uint32_t crc() const { return crc_; }

 private:
  const Sink& sink_;
  uint8_t* buffer_;
  size_t capacity_;
  size_t used_ = 0;
  size_t bytes_ = 0;
  uint32_t crc_ = 0;
};

struct BufferContext {
  uint8_t* data = nullptr;
  size_t capacity = 0;
  size_t position = 0;
  size_t size = 0;
};

bool bufferWrite(void* context, const uint8_t* data, size_t length) {
  BufferContext* buffer = static_cast<BufferContext*>(context);
  if (buffer->position + length > buffer->capacity) {
    return false;
  }
  std::memcpy(buffer->data + buffer->position, data, length);
  buffer->position += length;
  if (buffer->position > buffer->size) {
    buffer->size = buffer->position;
  }
  return true;
}

bool bufferSeek(void* context, uint32_t offset) {
  BufferContext* buffer = static_cast<BufferContext*>(context);
  if (offset > buffer->capacity) {
    return false;
  }
  buffer->position = offset;
  return true;
}

}  // namespace

void encodeHeader(uint8_t* out, const Header& header) {
  if (!out) {
    return;
  }
  std::memset(out, 0, kHeaderSize);
  writeU32(out, kMagic);
  writeU16(out + 4, header.version);
  writeU16(out + 6, header.flags);
  writeU16(out + 8, header.width);
  writeU16(out + 10, header.height);
  writeU32(out + 12, header.rleLength);
  writeU32(out + 16, header.thumbnailOffset);
  writeU32(out + 20, header.crc);
}

bool decodeHeader(const uint8_t* data, size_t length, Header& out) {
  if (!data || length < kHeaderSize || readU32(data) != kMagic) {
    return false;
  }

  Header header;
  header.version = readU16(data + 4);
  header.flags = readU16(data + 6);
  header.width = readU16(data + 8);
  header.height = readU16(data + 10);
  header.rleLength = readU32(data + 12);
  header.thumbnailOffset = readU32(data + 16);
  header.crc = readU32(data + 20);

  if (header.version != kVersion || header.flags != 0 || header.width == 0 ||
      header.height == 0 || header.rleLength == 0) {
    return false;
  }
  for (size_t i = 0; i < kReservedSize; ++i) {
    if (data[24 + i] != 0) {
      return false;
    }
  }
  if (static_cast<uint64_t>(header.thumbnailOffset) !=
      static_cast<uint64_t>(kHeaderSize) + header.rleLength) {
    return false;
  }

  out = header;
  return true;
}

bool buildThumbnail(const uint16_t* pixels, size_t stride, uint16_t width, uint16_t height,
                    uint16_t* out) {
  if (!pixels || !out || width == 0 || height == 0 || stride < width) {
    return false;
  }
  for (int ty = 0; ty < kThumbnailHeight; ++ty) {
    const size_t sy =
        static_cast<size_t>(ty) * height / static_cast<size_t>(kThumbnailHeight);
    const uint16_t* row = pixels + sy * stride;
    for (int tx = 0; tx < kThumbnailWidth; ++tx) {
      const size_t sx =
          static_cast<size_t>(tx) * width / static_cast<size_t>(kThumbnailWidth);
      out[static_cast<size_t>(ty) * kThumbnailWidth + tx] = row[sx];
    }
  }
  return true;
}

bool decodeThumbnail(const uint8_t* data, size_t length, const Header& header, uint16_t* out) {
  if (!data || !out ||
      static_cast<uint64_t>(header.thumbnailOffset) + kThumbnailBytes > length) {
    return false;
  }
  const uint8_t* thumb = data + header.thumbnailOffset;
  for (size_t i = 0; i < kThumbnailPixels; ++i) {
    out[i] = readU16(thumb + i * 2);
  }
  return true;
}

bool encode(const uint16_t* pixels, size_t stride, uint16_t width, uint16_t height,
            const Sink& sink) {
  if (!pixels || !sink.write || !sink.seek || width == 0 || height == 0 || stride < width) {
    return false;
  }
  if (static_cast<uint64_t>(width) * height > 0x7FFFFFFFu) {
    return false;
  }

  Header header;
  header.width = width;
  header.height = height;
  uint8_t headerBytes[kHeaderSize] = {};
  encodeHeader(headerBytes, header);
  if (!sink.write(sink.context, headerBytes, sizeof(headerBytes))) {
    return false;
  }

  uint8_t writeBuffer[kWriteBufferBytes];
  BufferedWriter writer(sink, writeBuffer, sizeof(writeBuffer));

  // RLE is emitted in bounded row chunks so no width-sized allocation is
  // needed. Runs that cross a chunk boundary are simply split into two runs.
  uint8_t rleBuffer[2 + kRleChunkPixels * 2];
  for (size_t y = 0; y < height; ++y) {
    const uint16_t* row = pixels + y * stride;
    for (size_t x = 0; x < width; x += kRleChunkPixels) {
      const size_t remaining = width - x;
      const size_t chunk = remaining < kRleChunkPixels ? remaining : kRleChunkPixels;
      const size_t encoded = rleEncode(row + x, chunk, rleBuffer, sizeof(rleBuffer));
      if (encoded == 0 || !writer.write(rleBuffer, encoded)) {
        return false;
      }
    }
  }

  const size_t rleLength = writer.bytes();
  const size_t thumbnailOffset = kHeaderSize + rleLength;
  if (rleLength > 0xFFFFFFFFu || thumbnailOffset > 0xFFFFFFFFu) {
    return false;
  }

  uint8_t thumbnailRow[kThumbnailWidth * 2];
  for (int ty = 0; ty < kThumbnailHeight; ++ty) {
    const size_t sy =
        static_cast<size_t>(ty) * height / static_cast<size_t>(kThumbnailHeight);
    const uint16_t* row = pixels + sy * stride;
    for (int tx = 0; tx < kThumbnailWidth; ++tx) {
      const size_t sx =
          static_cast<size_t>(tx) * width / static_cast<size_t>(kThumbnailWidth);
      writeU16(thumbnailRow + tx * 2, row[sx]);
    }
    if (!writer.write(thumbnailRow, sizeof(thumbnailRow))) {
      return false;
    }
  }

  if (!writer.flush()) {
    return false;
  }

  return patchU32(sink, 12, static_cast<uint32_t>(rleLength)) &&
         patchU32(sink, 16, static_cast<uint32_t>(thumbnailOffset)) &&
         patchU32(sink, 20, writer.crc());
}

size_t encodeToBuffer(const uint16_t* pixels, size_t stride, uint16_t width, uint16_t height,
                      uint8_t* dst, size_t dstCapacity) {
  if (!dst || dstCapacity < kHeaderSize) {
    return 0;
  }
  BufferContext context{dst, dstCapacity, 0, 0};
  const Sink sink{bufferWrite, bufferSeek, &context};
  if (!encode(pixels, stride, width, height, sink)) {
    return 0;
  }
  return context.size;
}

bool decode(const uint8_t* data, size_t length, uint16_t* pixels, DocumentInfo& info) {
  if (!data || !pixels) {
    return false;
  }

  Header header;
  if (!decodeHeader(data, length, header)) {
    return false;
  }

  const uint64_t expected = static_cast<uint64_t>(header.thumbnailOffset) + kThumbnailBytes;
  if (expected != length) {
    return false;
  }
  if (crc32(data + kHeaderSize, length - kHeaderSize) != header.crc) {
    return false;
  }

  const size_t count = static_cast<size_t>(header.width) * header.height;
  // Validate the token stream before touching the destination so a malformed
  // but CRC-valid file cannot leave the canvas half-written.
  if (!rleValidate(data + kHeaderSize, header.rleLength, count) ||
      !rleDecode(data + kHeaderSize, header.rleLength, pixels, count)) {
    return false;
  }

  info.width = header.width;
  info.height = header.height;
  info.crc = header.crc;
  return true;
}

}  // namespace paint::pnt
