#pragma once

// Touch gesture state machine: debounced down/move/up transitions plus tap and
// one-shot long-press recognition. Header-only and Arduino-free so the native
// tests can drive it directly.

#include <cstdint>

#include "config.h"
#include "input/touch.h"

namespace paint {

struct TouchFrame {
  bool pressed = false;   // current debounced state
  bool down = false;      // transition this frame
  bool move = false;      // movement passed the threshold
  bool up = false;        // transition this frame
  bool tap = false;       // released without moving or long-pressing
  bool longPress = false; // fired once while held still

  int16_t x = 0;
  int16_t y = 0;
  int16_t startX = 0;
  int16_t startY = 0;

  // Raw controller values for the current sample, filled in by the main loop.
  // Used by the guided calibration screen; zero when the panel is released.
  uint16_t rawX = 0;
  uint16_t rawY = 0;

  uint32_t durationMs = 0;
};

class TouchStateMachine {
 public:
  void reset();

  TouchFrame update(const TouchPoint& sample, uint32_t nowMs);

 private:
  static int32_t distanceSquared(int16_t ax, int16_t ay, int16_t bx, int16_t by) {
    const int32_t dx = static_cast<int32_t>(ax) - bx;
    const int32_t dy = static_cast<int32_t>(ay) - by;
    return dx * dx + dy * dy;
  }

  bool pressed_ = false;
  int16_t x_ = 0;
  int16_t y_ = 0;
  int16_t startX_ = 0;
  int16_t startY_ = 0;
  int16_t lastMoveX_ = 0;
  int16_t lastMoveY_ = 0;
  uint32_t downMs_ = 0;
  int32_t maxMoveSq_ = 0;
  bool longPressFired_ = false;
  uint8_t pressCount_ = 0;
  uint8_t releaseCount_ = 0;
};

inline void TouchStateMachine::reset() {
  *this = TouchStateMachine{};
}

inline TouchFrame TouchStateMachine::update(const TouchPoint& sample, uint32_t nowMs) {
  TouchFrame frame;

  constexpr int32_t kMoveThresholdSq =
      config::kTouchMoveThreshold * config::kTouchMoveThreshold;
  constexpr int32_t kTapMaxMoveSq = config::kTouchTapMaxMove * config::kTouchTapMaxMove;

  if (!pressed_) {
    if (!sample.pressed) {
      pressCount_ = 0;
      frame.x = x_;
      frame.y = y_;
      return frame;
    }

    // Require a couple of consecutive pressed samples so a single noisy
    // conversion cannot start a gesture.
    if (static_cast<uint8_t>(pressCount_ + 1) < config::kTouchDebounceSamples) {
      ++pressCount_;
      frame.x = sample.x;
      frame.y = sample.y;
      return frame;
    }

    pressCount_ = 0;
    releaseCount_ = 0;
    pressed_ = true;
    downMs_ = nowMs;
    startX_ = sample.x;
    startY_ = sample.y;
    x_ = sample.x;
    y_ = sample.y;
    lastMoveX_ = x_;
    lastMoveY_ = y_;
    maxMoveSq_ = 0;
    longPressFired_ = false;

    frame.pressed = true;
    frame.down = true;
    frame.x = x_;
    frame.y = y_;
    frame.startX = startX_;
    frame.startY = startY_;
    return frame;
  }

  if (!sample.pressed) {
    // Symmetric release debounce; ignore one-sample dropouts.
    if (static_cast<uint8_t>(releaseCount_ + 1) < config::kTouchDebounceSamples) {
      ++releaseCount_;
      frame.pressed = true;
      frame.x = x_;
      frame.y = y_;
      frame.startX = startX_;
      frame.startY = startY_;
      frame.durationMs = nowMs - downMs_;
      return frame;
    }

    releaseCount_ = 0;
    frame.up = true;
    frame.x = x_;
    frame.y = y_;
    frame.startX = startX_;
    frame.startY = startY_;
    frame.durationMs = nowMs - downMs_;
    frame.tap = !longPressFired_ && maxMoveSq_ <= kTapMaxMoveSq;

    pressed_ = false;
    longPressFired_ = false;
    return frame;
  }

  releaseCount_ = 0;
  x_ = sample.x;
  y_ = sample.y;

  const int32_t fromStart = distanceSquared(x_, y_, startX_, startY_);
  if (fromStart > maxMoveSq_) {
    maxMoveSq_ = fromStart;
  }

  if (!longPressFired_ && (nowMs - downMs_) >= config::kTouchLongPressMs &&
      maxMoveSq_ <= kTapMaxMoveSq) {
    longPressFired_ = true;
    frame.longPress = true;
  }

  if (distanceSquared(x_, y_, lastMoveX_, lastMoveY_) >= kMoveThresholdSq) {
    frame.move = true;
    lastMoveX_ = x_;
    lastMoveY_ = y_;
  }

  frame.pressed = true;
  frame.x = x_;
  frame.y = y_;
  frame.startX = startX_;
  frame.startY = startY_;
  frame.durationMs = nowMs - downMs_;
  return frame;
}

}  // namespace paint
