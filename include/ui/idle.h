#pragma once

// Idle backlight state machine. Header-only so the native tests can exercise
// the dim/off/wake thresholds without an Arduino build.

#include <cstdint>

#include "config.h"

namespace paint {

enum class IdleLevel : uint8_t {
  Active,
  Dimmed,
  Off,
};

class IdleController {
 public:
  void reset(uint32_t nowMs) {
    lastTouchMs_ = nowMs;
    level_ = IdleLevel::Active;
  }

  // Any touch activity, including the initial down and the final up, keeps
  // the display awake and restores full brightness.
  void noteTouch(uint32_t nowMs) {
    lastTouchMs_ = nowMs;
    level_ = IdleLevel::Active;
  }

  IdleLevel update(uint32_t nowMs) {
    if (level_ == IdleLevel::Off) {
      return level_;
    }
    const uint32_t idle = nowMs - lastTouchMs_;
    const IdleLevel next =
        idle >= config::kIdleOffMs
            ? IdleLevel::Off
            : (idle >= config::kIdleDimMs ? IdleLevel::Dimmed : IdleLevel::Active);
    level_ = next;
    return level_;
  }

  IdleLevel level() const { return level_; }
  uint32_t lastTouchMs() const { return lastTouchMs_; }

  // The first press while the screen is off only wakes the display. Main uses
  // this to swallow the rest of that gesture so it cannot activate a control
  // hidden under the user's finger.
  bool shouldWakeAndConsume(bool pressed) const {
    return level_ == IdleLevel::Off && pressed;
  }

 private:
  uint32_t lastTouchMs_ = 0;
  IdleLevel level_ = IdleLevel::Active;
};

}  // namespace paint
