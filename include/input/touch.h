#pragma once

// Touch input abstraction. The shipped panel uses an HR2046, which is
// register-compatible with the XPT2046.

#include <cstdint>

#include "input/calibration.h"

namespace paint {

struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
};

struct TouchRaw {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t z = 0;
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

  // Reads raw and calibrated values from a single controller sample.
  bool read(TouchPoint& out, TouchRaw& rawOut);

  // Raw 12-bit controller values, useful for bring-up and calibration.
  bool readRaw(TouchRaw& out);

  void setCalibration(const Calibration& cal) { cal_ = cal; }
  const Calibration& calibration() const { return cal_; }

 private:
  Calibration cal_;
  int16_t emaX_ = 0;
  int16_t emaY_ = 0;
  bool wasPressed_ = false;
};

}  // namespace paint
