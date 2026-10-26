#pragma once

// Bounded, compressed undo/redo. Each action records the canvas tiles it
// touches. A tile is captured before the first write (RLE-compressed, or raw
// for noisy tiles); at commit time the captured "before" pixels are XORed with
// the live canvas to produce a compact delta. Undo and redo are the same
// operation: XOR the delta into the canvas.
//
// The pools are intentionally fixed-size and live in the object, so drawing
// never allocates. When the pools fill, the oldest action is evicted. RLE
// deltas make typical brush strokes small, so several long strokes fit in the
// same budget that used to hold only a few raw 32x32 tile snapshots.

#include <cstdint>
#include <cstring>

#include "config.h"
#include "paint/canvas.h"
#include "paint/geometry.h"

namespace paint {

class UndoStack {
 public:
  static constexpr int kRleRecords = 44;
  static constexpr int kRawRecords = 7;
  static constexpr int kMaxTileRecords = kRleRecords + kRawRecords;
  static constexpr int kRleMaxBytes = 1024;
  static constexpr int kMaxActions = 16;
  // Long multi-second squiggles touch more than 32 tiles. Allow one action to
  // use the whole 51-record pool; older actions are evicted as needed, which is
  // preferable to refusing the newest stroke. A single action touching more
  // than every tile still overflows safely instead of corrupting history.
  static constexpr int kMaxTilesPerAction = kRleRecords + kRawRecords;
  static_assert(kMaxTilesPerAction <= kMaxTileRecords,
                "one action cannot reference more records than the pool holds");
  static constexpr int kTilesX =
      (config::kCanvasWidth + config::kTileSize - 1) / config::kTileSize;
  static constexpr int kTilesY =
      (config::kCanvasHeight + config::kTileSize - 1) / config::kTileSize;
  static constexpr int kTilePixels = config::kTileSize * config::kTileSize;

  bool beginAction() {
    if (currentActive_) {
      discardCurrent();
    }
    discardUndoneTail();
    currentActive_ = true;
    currentCount_ = 0;
    currentValid_ = true;
    overflow_ = false;
    activeCanvas_ = nullptr;
    return true;
  }

  // Capture every tile touched by rect before the caller writes to it.
  void captureRect(Canvas& canvas, const Rect& rect) {
    if (!currentActive_ || !currentValid_ || !canvas.ready()) {
      return;
    }
    const Rect r = rect.intersect(canvas.bounds());
    if (r.empty()) {
      return;
    }
    if (!activeCanvas_) {
      activeCanvas_ = &canvas;
    }

    const int tx0 = (r.x - canvas.bounds().x) / config::kTileSize;
    const int tx1 = (r.right() - 1 - canvas.bounds().x) / config::kTileSize;
    const int ty0 = (r.y - canvas.bounds().y) / config::kTileSize;
    const int ty1 = (r.bottom() - 1 - canvas.bounds().y) / config::kTileSize;

    for (int ty = ty0; ty <= ty1; ++ty) {
      for (int tx = tx0; tx <= tx1; ++tx) {
        captureTile(canvas, ty * kTilesX + tx);
        if (!currentValid_) {
          return;
        }
      }
    }
  }

  // Convert the captured "before" tiles into XOR deltas and publish the action.
  bool commit() {
    if (!currentActive_) {
      return false;
    }
    if (!currentValid_ || currentCount_ == 0 || !activeCanvas_ ||
        !activeCanvas_->ready()) {
      discardCurrent();
      return false;
    }

    Ref committed[kMaxTilesPerAction] = {};
    int committedCount = 0;
    bool ok = true;

    for (int i = 0; i < currentCount_ && ok; ++i) {
      TileRecord& record = records_[current_[i].record];
      if (!record.used) {
        continue;
      }

      decodeRecord(record, scratch_);
      const Rect tileBox = tileRect(record.tile, activeCanvas_->bounds());
      const Rect r = tileBox.intersect(activeCanvas_->bounds());
      if (r.empty()) {
        releaseRecord(record);
        continue;
      }

      bool changed = false;
      for (int y = r.y; y < r.bottom(); ++y) {
        const uint16_t* row =
            activeCanvas_->pixels() + y * activeCanvas_->stride() + r.x;
        for (int x = 0; x < r.w; ++x) {
          const int index = (y - tileBox.y) * config::kTileSize +
                            (r.x - tileBox.x) + x;
          scratch_[index] ^= row[x];
          if (scratch_[index] != 0) {
            changed = true;
          }
        }
      }
      if (!changed) {
        releaseRecord(record);
        continue;
      }

      uint8_t encoded[kRleMaxBytes];
      const int length = encodeRle(scratch_, encoded, kRleMaxBytes);
      if (record.compressed) {
        if (length >= 0) {
          std::memcpy(rle_[record.slot], encoded,
                      static_cast<size_t>(length));
          record.length = static_cast<uint16_t>(length);
        } else {
          // The delta is too complex for an RLE slot; move this record to a
          // raw slot. This is rare (smooth brush strokes stay compressible).
          const int rawSlot = allocateRawSlot();
          if (rawSlot < 0) {
            ok = false;
            overflow_ = true;
            break;
          }
          rleUsed_[record.slot] = false;
          record.compressed = 0;
          record.slot = static_cast<uint8_t>(rawSlot);
          record.length = 0;
          std::memcpy(raw_[rawSlot], scratch_, sizeof(scratch_));
        }
      } else {
        std::memcpy(raw_[record.slot], scratch_, sizeof(scratch_));
        record.length = 0;
      }

      committed[committedCount++] = current_[i];
    }

    if (!ok || committedCount == 0) {
      discardCurrent();
      return false;
    }

    if (actionCount_ >= kMaxActions) {
      freeAction(0);
    }
    Action& action = actions_[actionCount_];
    action.count = static_cast<uint8_t>(committedCount);
    action.undone = false;
    for (int i = 0; i < committedCount; ++i) {
      action.refs[i] = committed[i];
    }
    ++actionCount_;

    currentActive_ = false;
    currentCount_ = 0;
    currentValid_ = true;
    activeCanvas_ = nullptr;
    return true;
  }

  bool undo(Canvas& canvas) {
    if (!canvas.ready()) {
      return false;
    }
    int index = -1;
    for (int i = 0; i < actionCount_; ++i) {
      if (!actions_[i].undone) {
        index = i;
      }
    }
    if (index < 0) {
      return false;
    }

    Action& action = actions_[index];
    for (int i = action.count - 1; i >= 0; --i) {
      applyRecord(canvas, records_[action.refs[i].record]);
    }
    action.undone = true;
    return true;
  }

  bool redo(Canvas& canvas) {
    if (!canvas.ready()) {
      return false;
    }
    int index = -1;
    for (int i = 0; i < actionCount_; ++i) {
      if (actions_[i].undone) {
        index = i;
        break;
      }
    }
    if (index < 0) {
      return false;
    }

    Action& action = actions_[index];
    for (int i = 0; i < action.count; ++i) {
      applyRecord(canvas, records_[action.refs[i].record]);
    }
    action.undone = false;
    return true;
  }

  void clear() {
    discardCurrent();
    for (int i = 0; i < actionCount_; ++i) {
      releaseAction(actions_[i]);
    }
    actionCount_ = 0;
    currentActive_ = false;
    currentCount_ = 0;
    currentValid_ = true;
    overflow_ = false;
    activeCanvas_ = nullptr;
    std::memset(rleUsed_, 0, sizeof(rleUsed_));
    std::memset(rawUsed_, 0, sizeof(rawUsed_));
    for (int i = 0; i < kMaxTileRecords; ++i) {
      records_[i].used = 0;
      records_[i].tile = -1;
      records_[i].length = 0;
    }
  }

  bool canUndo() const {
    for (int i = 0; i < actionCount_; ++i) {
      if (!actions_[i].undone) {
        return true;
      }
    }
    return false;
  }

  bool canRedo() const {
    for (int i = 0; i < actionCount_; ++i) {
      if (actions_[i].undone) {
        return true;
      }
    }
    return false;
  }

  int undoDepth() const {
    int depth = 0;
    for (int i = 0; i < actionCount_; ++i) {
      if (!actions_[i].undone) {
        ++depth;
      }
    }
    return depth;
  }

  int redoDepth() const {
    int depth = 0;
    for (int i = 0; i < actionCount_; ++i) {
      if (actions_[i].undone) {
        ++depth;
      }
    }
    return depth;
  }

  struct Usage {
    int actions = 0;  // published actions, including undone ones
    int records = 0;  // tile records currently allocated
    int bytes = 0;    // encoded bytes in the record pools
  };

  Usage usage() const {
    Usage result;
    result.actions = actionCount_;
    for (int i = 0; i < kMaxTileRecords; ++i) {
      if (records_[i].used) {
        ++result.records;
        result.bytes += records_[i].length;
      }
    }
    return result;
  }

  bool takeOverflow() {
    const bool value = overflow_;
    overflow_ = false;
    return value;
  }

 private:
  struct Ref {
    uint8_t record = 0;
    int16_t tile = -1;
  };

  struct TileRecord {
    int16_t tile = -1;
    uint16_t length = 0;
    uint8_t slot = 0;
    uint8_t used = 0;
    uint8_t compressed = 0;
  };

  struct Action {
    Ref refs[kMaxTilesPerAction] = {};
    uint8_t count = 0;
    bool undone = false;
  };

  static int encodeRle(const uint16_t* pixels, uint8_t* out, int capacity) {
    int input = 0;
    int output = 0;
    while (input < kTilePixels) {
      const uint16_t color = pixels[input];
      int run = 1;
      while (input + run < kTilePixels && pixels[input + run] == color) {
        ++run;
      }
      if (output + 4 > capacity) {
        return -1;
      }
      out[output++] = static_cast<uint8_t>(run & 0xFF);
      out[output++] = static_cast<uint8_t>((run >> 8) & 0xFF);
      out[output++] = static_cast<uint8_t>(color & 0xFF);
      out[output++] = static_cast<uint8_t>((color >> 8) & 0xFF);
      input += run;
    }
    return output;
  }

  static void decodeRle(const uint8_t* in, int length, uint16_t* pixels) {
    int input = 0;
    int output = 0;
    while (input + 4 <= length && output < kTilePixels) {
      const int run = static_cast<int>(in[input]) |
                      (static_cast<int>(in[input + 1]) << 8);
      const uint16_t color = static_cast<uint16_t>(
          static_cast<uint16_t>(in[input + 2]) |
          (static_cast<uint16_t>(in[input + 3]) << 8));
      input += 4;
      if (run <= 0) {
        break;
      }
      for (int i = 0; i < run && output < kTilePixels; ++i) {
        pixels[output++] = color;
      }
    }
    while (output < kTilePixels) {
      pixels[output++] = 0;
    }
  }

  Rect tileRect(int tile, const Rect& canvasBounds) const {
    const int col = tile % kTilesX;
    const int row = tile / kTilesX;
    return Rect{static_cast<int16_t>(canvasBounds.x + col * config::kTileSize),
                static_cast<int16_t>(canvasBounds.y + row * config::kTileSize),
                static_cast<int16_t>(config::kTileSize),
                static_cast<int16_t>(config::kTileSize)};
  }

  void captureTile(Canvas& canvas, int tile) {
    if (tile < 0 || tile >= kTilesX * kTilesY) {
      return;
    }
    for (int i = 0; i < currentCount_; ++i) {
      if (current_[i].tile == tile) {
        return;
      }
    }
    if (currentCount_ >= kMaxTilesPerAction) {
      currentValid_ = false;
      overflow_ = true;
      return;
    }

    const Rect tileBox = tileRect(tile, canvas.bounds());
    const Rect r = tileBox.intersect(canvas.bounds());
    std::memset(scratch_, 0, sizeof(scratch_));
    if (!r.empty()) {
      for (int y = r.y; y < r.bottom(); ++y) {
        std::memcpy(&scratch_[(y - tileBox.y) * config::kTileSize +
                              (r.x - tileBox.x)],
                    canvas.pixels() + y * canvas.stride() + r.x,
                    static_cast<size_t>(r.w) * sizeof(uint16_t));
      }
    }

    uint8_t encoded[kRleMaxBytes];
    const int length = encodeRle(scratch_, encoded, kRleMaxBytes);

    int record = -1;
    if (length >= 0) {
      record = allocateRecord(true);
      if (record >= 0) {
        TileRecord& dst = records_[record];
        std::memcpy(rle_[dst.slot], encoded, static_cast<size_t>(length));
        dst.length = static_cast<uint16_t>(length);
      }
    }
    if (record < 0) {
      record = allocateRecord(false);
      if (record < 0) {
        currentValid_ = false;
        overflow_ = true;
        return;
      }
      TileRecord& dst = records_[record];
      std::memcpy(raw_[dst.slot], scratch_, sizeof(scratch_));
      dst.length = 0;
    }

    TileRecord& dst = records_[record];
    dst.tile = static_cast<int16_t>(tile);
    current_[currentCount_].record = static_cast<uint8_t>(record);
    current_[currentCount_].tile = static_cast<int16_t>(tile);
    ++currentCount_;
  }

  void decodeRecord(const TileRecord& record, uint16_t* pixels) const {
    if (record.compressed) {
      decodeRle(rle_[record.slot], record.length, pixels);
    } else {
      std::memcpy(pixels, raw_[record.slot], sizeof(scratch_));
    }
  }

  void applyRecord(Canvas& canvas, TileRecord& record) {
    if (!record.used || record.tile < 0) {
      return;
    }
    const Rect tileBox = tileRect(record.tile, canvas.bounds());
    const Rect r = tileBox.intersect(canvas.bounds());
    if (r.empty()) {
      return;
    }

    if (record.compressed) {
      decodeRle(rle_[record.slot], record.length, scratch_);
    }
    const uint16_t* delta =
        record.compressed ? scratch_ : raw_[record.slot];

    for (int y = r.y; y < r.bottom(); ++y) {
      uint16_t* row = canvas.pixels() + y * canvas.stride() + r.x;
      for (int x = 0; x < r.w; ++x) {
        const int index = (y - tileBox.y) * config::kTileSize +
                          (r.x - tileBox.x) + x;
        row[x] ^= delta[index];
      }
    }
    canvas.markDirty(r);
  }

  int findFreeRecord() const {
    for (int i = 0; i < kMaxTileRecords; ++i) {
      if (!records_[i].used) {
        return i;
      }
    }
    return -1;
  }

  int findFreeRleSlot() const {
    for (int i = 0; i < kRleRecords; ++i) {
      if (!rleUsed_[i]) {
        return i;
      }
    }
    return -1;
  }

  int findFreeRawSlot() const {
    for (int i = 0; i < kRawRecords; ++i) {
      if (!rawUsed_[i]) {
        return i;
      }
    }
    return -1;
  }

  int allocateRecord(bool compressed) {
    while (true) {
      const int slot = compressed ? findFreeRleSlot() : findFreeRawSlot();
      if (slot >= 0) {
        const int record = findFreeRecord();
        if (record < 0) {
          if (compressed) {
            rleUsed_[slot] = false;
          } else {
            rawUsed_[slot] = false;
          }
          return -1;
        }
        if (compressed) {
          rleUsed_[slot] = true;
        } else {
          rawUsed_[slot] = true;
        }
        TileRecord& dst = records_[record];
        dst.used = true;
        dst.tile = -1;
        dst.length = 0;
        dst.slot = static_cast<uint8_t>(slot);
        dst.compressed = compressed ? 1 : 0;
        return record;
      }
      if (actionCount_ > 0) {
        freeAction(0);
        continue;
      }
      return -1;
    }
  }

  int allocateRawSlot() {
    while (true) {
      const int slot = findFreeRawSlot();
      if (slot >= 0) {
        rawUsed_[slot] = true;
        return slot;
      }
      if (actionCount_ > 0) {
        freeAction(0);
        continue;
      }
      return -1;
    }
  }

  void releaseRecord(TileRecord& record) {
    if (!record.used) {
      return;
    }
    if (record.compressed) {
      if (record.slot < kRleRecords) {
        rleUsed_[record.slot] = false;
      }
    } else if (record.slot < kRawRecords) {
      rawUsed_[record.slot] = false;
    }
    record.used = 0;
    record.tile = -1;
    record.length = 0;
  }

  void releaseAction(Action& action) {
    for (int i = 0; i < action.count; ++i) {
      const int record = action.refs[i].record;
      if (record >= 0 && record < kMaxTileRecords) {
        releaseRecord(records_[record]);
      }
    }
    action.count = 0;
    action.undone = false;
  }

  void freeAction(int index) {
    if (index < 0 || index >= actionCount_) {
      return;
    }
    releaseAction(actions_[index]);
    for (int i = index; i < actionCount_ - 1; ++i) {
      actions_[i] = actions_[i + 1];
    }
    --actionCount_;
    actions_[actionCount_].count = 0;
    actions_[actionCount_].undone = false;
  }

  void discardCurrent() {
    for (int i = 0; i < currentCount_; ++i) {
      const int record = current_[i].record;
      if (record >= 0 && record < kMaxTileRecords) {
        releaseRecord(records_[record]);
      }
    }
    currentCount_ = 0;
    currentActive_ = false;
    currentValid_ = true;
    activeCanvas_ = nullptr;
  }

  void discardUndoneTail() {
    while (actionCount_ > 0 && actions_[actionCount_ - 1].undone) {
      freeAction(actionCount_ - 1);
    }
  }

  // Storage is split so compressed records do not reserve a full 2 KB tile.
  uint8_t rle_[kRleRecords][kRleMaxBytes] = {};
  uint16_t raw_[kRawRecords][kTilePixels] = {};
  bool rleUsed_[kRleRecords] = {};
  bool rawUsed_[kRawRecords] = {};
  uint16_t scratch_[kTilePixels] = {};

  TileRecord records_[kMaxTileRecords];
  Action actions_[kMaxActions];
  Ref current_[kMaxTilesPerAction] = {};
  Canvas* activeCanvas_ = nullptr;
  int actionCount_ = 0;
  int currentCount_ = 0;
  bool currentActive_ = false;
  bool currentValid_ = true;
  bool overflow_ = false;
};

}  // namespace paint
