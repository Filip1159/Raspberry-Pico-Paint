#pragma once

// 4-point touch calibration.
// Coefficients are stored in Q16.16 fixed point. Header-only so the native
// tests can exercise the math without pulling in the Arduino build.

#include <cstdint>

#include "config.h"
#include "paint/geometry.h"

namespace paint {

struct Calibration {
  int32_t a = 0, b = 0, c = 0;  // x_screen = a*x_raw + b*y_raw + c
  int32_t d = 0, e = 0, f = 0;  // y_screen = d*x_raw + e*y_raw + f
};

namespace detail {

inline int16_t clampScreen(int32_t value, int16_t maxValue) {
  if (value < 0) {
    return 0;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return static_cast<int16_t>(value);
}

}  // namespace detail

// Build an axis-aligned calibration from the averaged raw coordinates of the
// left/right and top/bottom edges. This is the 4-point calibration used by the
// panel: two samples per axis reduce noise and handle either axis direction.
inline Calibration makeAxisCalibration(int32_t rawLeftX, int32_t rawRightX,
                                       int32_t rawTopY, int32_t rawBottomY,
                                       int16_t screenLeft, int16_t screenRight,
                                       int16_t screenTop, int16_t screenBottom) {
  Calibration cal;

  const int32_t dx = rawRightX - rawLeftX;
  const int32_t dy = rawBottomY - rawTopY;
  if (dx == 0 || dy == 0) {
    return cal;
  }

  cal.a = (static_cast<int32_t>(screenRight - screenLeft) * 65536) / dx;
  cal.c = static_cast<int32_t>(screenLeft) - (cal.a * rawLeftX) / 65536;

  cal.e = (static_cast<int32_t>(screenBottom - screenTop) * 65536) / dy;
  cal.f = static_cast<int32_t>(screenTop) - (cal.e * rawTopY) / 65536;

  return cal;
}

inline Point applyCalibration(const Calibration& cal, int32_t rawX, int32_t rawY) {
  const int32_t x = (cal.a * rawX + cal.b * rawY) / 65536 + cal.c;
  const int32_t y = (cal.d * rawX + cal.e * rawY) / 65536 + cal.f;

  return Point{detail::clampScreen(x, config::kScreenWidth - 1),
               detail::clampScreen(y, config::kScreenHeight - 1)};
}

}  // namespace paint
