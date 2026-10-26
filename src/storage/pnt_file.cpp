#include "storage/pnt_file.h"

#include <LittleFS.h>

#include <cstring>

#include "config.h"
#include "paint/crc32.h"
#include "paint/pnt.h"

namespace paint {
namespace {

constexpr size_t kReadBufferBytes = 512;
constexpr size_t kMaxPathBytes = 96;

bool writeSink(void* context, const uint8_t* data, size_t length) {
  File* file = static_cast<File*>(context);
  return file->write(data, length) == length;
}

bool seekSink(void* context, uint32_t offset) {
  File* file = static_cast<File*>(context);
  return file->seek(offset);
}

// Replaces the extension with .tmp, matching the /paint/imgNNNN.tmp layout.
bool makeTempPath(const char* path, char* out, size_t capacity) {
  const size_t length = std::strlen(path);
  if (length + 5 > capacity) {
    return false;
  }

  std::memcpy(out, path, length + 1);
  char* slash = std::strrchr(out, '/');
  char* dot = std::strrchr(out, '.');
  if (dot && (!slash || dot > slash)) {
    *dot = '\0';
  }
  std::strcat(out, ".tmp");
  return true;
}

bool ensureParentDirectory(const char* path) {
  const char* slash = std::strrchr(path, '/');
  if (!slash || slash == path) {
    return true;
  }

  char directory[kMaxPathBytes];
  const size_t length = static_cast<size_t>(slash - path);
  if (length >= sizeof(directory)) {
    return false;
  }
  std::memcpy(directory, path, length);
  directory[length] = '\0';

  return LittleFS.exists(directory) || LittleFS.mkdir(directory);
}

bool expectedFileSize(const pnt::Header& header, size_t& out) {
  const uint64_t size =
      static_cast<uint64_t>(header.thumbnailOffset) + pnt::kThumbnailBytes;
  if (size > 0xFFFFFFFFu) {
    return false;
  }
  out = static_cast<size_t>(size);
  return true;
}

bool verifyCrc(File& file, const pnt::Header& header, size_t fileSize) {
  if (!file.seek(pnt::kHeaderSize)) {
    return false;
  }

  uint8_t buffer[kReadBufferBytes];
  size_t remaining = fileSize - pnt::kHeaderSize;
  uint32_t crc = 0;
  while (remaining > 0) {
    const size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const int read = file.read(buffer, want);
    if (read <= 0) {
      return false;
    }
    crc = crc32Update(crc, buffer, static_cast<size_t>(read));
    remaining -= static_cast<size_t>(read);
  }
  return crc == header.crc;
}

// Small buffered reader so RLE tokens can straddle file reads.
class ByteReader {
 public:
  ByteReader(File& file, size_t length, uint8_t* buffer, size_t capacity)
      : file_(file), remaining_(length), buffer_(buffer), capacity_(capacity) {}

  bool readU8(uint8_t& out) {
    if (position_ == filled_ && !refill()) {
      return false;
    }
    out = buffer_[position_++];
    return true;
  }

  bool readU16(uint16_t& out) {
    uint8_t low = 0;
    uint8_t high = 0;
    if (!readU8(low) || !readU8(high)) {
      return false;
    }
    out = static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8));
    return true;
  }

  bool skip(size_t count) {
    while (count > 0) {
      if (position_ == filled_ && !refill()) {
        return false;
      }
      const size_t available = filled_ - position_;
      const size_t take = count < available ? count : available;
      position_ += take;
      count -= take;
    }
    return true;
  }

  bool exhausted() const { return remaining_ == 0 && position_ == filled_; }

 private:
  bool refill() {
    if (remaining_ == 0) {
      return false;
    }
    const size_t want = remaining_ < capacity_ ? remaining_ : capacity_;
    const int read = file_.read(buffer_, want);
    if (read <= 0) {
      return false;
    }
    filled_ = static_cast<size_t>(read);
    position_ = 0;
    remaining_ -= filled_;
    return true;
  }

  File& file_;
  size_t remaining_;
  uint8_t* buffer_;
  size_t capacity_;
  size_t filled_ = 0;
  size_t position_ = 0;
};

// Walks one RLE stream. When pixels is non-null the pixels are written with
// row stride; otherwise the stream is only validated. Returns true only when
// all pixels are produced and the RLE region is consumed exactly.
bool readRle(File& file, const pnt::Header& header, uint16_t* pixels, size_t stride) {
  uint8_t buffer[kReadBufferBytes];
  ByteReader reader(file, header.rleLength, buffer, sizeof(buffer));
  const size_t total = static_cast<size_t>(header.width) * header.height;
  size_t written = 0;
  size_t x = 0;
  size_t y = 0;

  auto emit = [&](uint16_t value) {
    if (pixels) {
      pixels[y * stride + x] = value;
      if (++x == header.width) {
        x = 0;
        ++y;
      }
    }
    ++written;
  };

  while (written < total) {
    uint16_t token = 0;
    if (!reader.readU16(token)) {
      return false;
    }

    if (token & 0x8000u) {
      const size_t length = token & 0x7FFFu;
      uint16_t value = 0;
      if (length == 0 || length > total - written || !reader.readU16(value)) {
        return false;
      }
      if (pixels) {
        for (size_t i = 0; i < length; ++i) {
          emit(value);
        }
      } else {
        written += length;
      }
    } else {
      const size_t length = token;
      if (length == 0 || length > total - written) {
        return false;
      }
      for (size_t i = 0; i < length; ++i) {
        uint16_t value = 0;
        if (!reader.readU16(value)) {
          return false;
        }
        emit(value);
      }
    }
  }

  return reader.exhausted();
}

bool verifyPntFile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }

  uint8_t headerBytes[pnt::kHeaderSize];
  if (file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) {
    file.close();
    return false;
  }

  pnt::Header header;
  size_t expected = 0;
  if (!pnt::decodeHeader(headerBytes, sizeof(headerBytes), header) ||
      header.width != config::kCanvasWidth ||
      header.height != config::kCanvasHeight ||
      !expectedFileSize(header, expected) || file.size() != expected ||
      !verifyCrc(file, header, expected) || !file.seek(pnt::kHeaderSize)) {
    file.close();
    return false;
  }

  const bool valid = readRle(file, header, nullptr, 0);
  file.close();
  return valid;
}

}  // namespace

bool savePnt(const char* path, const uint16_t* pixels, size_t stride, uint16_t width,
             uint16_t height) {
  if (!path || !*path || !pixels || width == 0 || height == 0 || stride < width) {
    return false;
  }
  if (!ensureParentDirectory(path)) {
    return false;
  }

  char tempPath[kMaxPathBytes];
  if (!makeTempPath(path, tempPath, sizeof(tempPath))) {
    return false;
  }

  LittleFS.remove(tempPath);
  File file = LittleFS.open(tempPath, "w");
  if (!file) {
    return false;
  }

  const pnt::Sink sink{writeSink, seekSink, &file};
  const bool encoded = pnt::encode(pixels, stride, width, height, sink);
  file.flush();
  file.close();

  if (!encoded || !verifyPntFile(tempPath)) {
    LittleFS.remove(tempPath);
    return false;
  }

  // LittleFS rename atomically replaces an existing destination. Never remove
  // the destination first: if the rename fails, the previous good file must
  // survive untouched.
  if (!LittleFS.rename(tempPath, path)) {
    LittleFS.remove(tempPath);
    return false;
  }
  return true;
}

bool loadPnt(const char* path, uint16_t* pixels, size_t stride, uint16_t width,
             uint16_t height, DocumentInfo& info) {
  if (!path || !*path || !pixels || width == 0 || height == 0) {
    return false;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }

  uint8_t headerBytes[pnt::kHeaderSize];
  if (file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) {
    file.close();
    return false;
  }

  pnt::Header header;
  size_t expected = 0;
  if (!pnt::decodeHeader(headerBytes, sizeof(headerBytes), header) ||
      header.width != width || header.height != height || stride < width ||
      !expectedFileSize(header, expected) || file.size() != expected ||
      !verifyCrc(file, header, expected) || !file.seek(pnt::kHeaderSize) ||
      !readRle(file, header, pixels, stride)) {
    file.close();
    return false;
  }

  file.close();
  info.width = header.width;
  info.height = header.height;
  info.crc = header.crc;
  return true;
}

bool isPntFileValid(const char* path) {
  return path && *path && verifyPntFile(path);
}

bool drawPntThumbnail(const char* path, uint16_t* fb, int fbWidth, const Rect& clip,
                      const Rect& dest) {
  if (!path || !*path || !fb || fbWidth <= 0 || dest.w <= 0 || dest.h <= 0) {
    return false;
  }

  const Rect area = dest.intersect(clip);
  if (area.empty()) {
    return true;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }

  uint8_t headerBytes[pnt::kHeaderSize];
  if (file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) {
    file.close();
    return false;
  }

  pnt::Header header;
  size_t expected = 0;
  if (!pnt::decodeHeader(headerBytes, sizeof(headerBytes), header) ||
      !expectedFileSize(header, expected) || file.size() != expected) {
    file.close();
    return false;
  }

  constexpr size_t kRowBytes = pnt::kThumbnailWidth * sizeof(uint16_t);
  uint16_t source[pnt::kThumbnailWidth];
  int cachedRow = -1;

  for (int y = area.y; y < area.bottom(); ++y) {
    int sourceY = ((y - dest.y) * pnt::kThumbnailHeight) / dest.h;
    if (sourceY < 0) {
      sourceY = 0;
    } else if (sourceY >= pnt::kThumbnailHeight) {
      sourceY = pnt::kThumbnailHeight - 1;
    }

    if (sourceY != cachedRow) {
      const uint32_t offset =
          header.thumbnailOffset + static_cast<uint32_t>(sourceY) * kRowBytes;
      if (!file.seek(offset) ||
          file.read(reinterpret_cast<uint8_t*>(source), sizeof(source)) !=
              static_cast<int>(sizeof(source))) {
        file.close();
        return false;
      }
      cachedRow = sourceY;
    }

    uint16_t* row = fb + y * fbWidth;
    for (int x = area.x; x < area.right(); ++x) {
      int sourceX = ((x - dest.x) * pnt::kThumbnailWidth) / dest.w;
      if (sourceX < 0) {
        sourceX = 0;
      } else if (sourceX >= pnt::kThumbnailWidth) {
        sourceX = pnt::kThumbnailWidth - 1;
      }
      row[x] = source[sourceX];
    }
  }

  file.close();
  return true;
}

}  // namespace paint
