#pragma once

// Touch input abstraction. The shipped panel uses an HR2046, which is
// register-compatible with the XPT2046.

#include <cstdint>

namespace paint {

struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
};

class ITouch {
 public:
  virtual ~ITouch() = default;
  virtual bool begin() = 0;
  virtual bool read(TouchPoint& out) = 0;
};

class Xpt2046Touch : public ITouch {
 public:
  bool begin() override;
  bool read(TouchPoint& out) override;
};

}  // namespace paint
