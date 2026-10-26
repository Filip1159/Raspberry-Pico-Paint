#pragma once

// Small, Arduino-free autosave scheduler. The main loop still owns the actual
// LittleFS write; this only decides when a changed canvas is due.
//
// - the first change arms the timer for 15 s;
// - further changes while armed do not postpone it (at most 15 s of loss);
// - a successful save clears the pending flag, so an idle dirty document is
//   not rewritten again until it changes;
// - a failed save retries after another interval.

#include <cstdint>

namespace paint {

class AutosaveTimer {
 public:
  static constexpr uint32_t kIntervalMs = 15000;

  void changed(uint32_t nowMs) {
    if (!pending_) {
      pending_ = true;
      dueMs_ = nowMs + kIntervalMs;
    }
  }

  bool due(uint32_t nowMs) const {
    return pending_ && static_cast<int32_t>(nowMs - dueMs_) >= 0;
  }

  void saved() { reset(); }

  void retry(uint32_t nowMs) {
    if (pending_) {
      dueMs_ = nowMs + kIntervalMs;
    }
  }

  void reset() {
    pending_ = false;
    dueMs_ = 0;
  }

  bool pending() const { return pending_; }

 private:
  bool pending_ = false;
  uint32_t dueMs_ = 0;
};

}  // namespace paint
