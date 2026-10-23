#pragma once

// 4-point touch calibration.
// Coefficients are stored in Q16.16 fixed point. Header-only so the native
// tests can exercise the math without pulling in the Arduino build.

#include <cmath>
#include <cstdint>

#include "config.h"
#include "paint/geometry.h"

namespace paint {

struct Calibration {
  int32_t a = 0, b = 0, c = 0;  // x_screen = a*x_raw + b*y_raw + c
  int32_t d = 0, e = 0, f = 0;  // y_screen = d*x_raw + e*y_raw + f
};

struct CalibrationPoint {
  int32_t rawX = 0;
  int32_t rawY = 0;
  int16_t screenX = 0;
  int16_t screenY = 0;
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

// Solve the full affine transform from four raw/screen point pairs using
// least squares. This handles rotated, mirrored and slightly skewed panels;
// the raw axes on the 4" module are rotated 90 degrees relative to the display.
inline Calibration makeFourPointCalibration(const CalibrationPoint points[4]) {
  Calibration cal;

  double meanRawX = 0.0;
  double meanRawY = 0.0;
  double meanScreenX = 0.0;
  double meanScreenY = 0.0;
  for (int i = 0; i < 4; ++i) {
    meanRawX += points[i].rawX;
    meanRawY += points[i].rawY;
    meanScreenX += points[i].screenX;
    meanScreenY += points[i].screenY;
  }
  meanRawX /= 4.0;
  meanRawY /= 4.0;
  meanScreenX /= 4.0;
  meanScreenY /= 4.0;

  // Center the raw coordinates before solving. Raw values are in the
  // thousands while screen values are in the hundreds; centering keeps the
  // normal equations well conditioned.
  double rawX2 = 0.0;
  double rawXY = 0.0;
  double rawY2 = 0.0;
  double rawXScreenX = 0.0;
  double rawYScreenX = 0.0;
  double rawXScreenY = 0.0;
  double rawYScreenY = 0.0;

  for (int i = 0; i < 4; ++i) {
    const double rx = points[i].rawX - meanRawX;
    const double ry = points[i].rawY - meanRawY;
    const double sx = points[i].screenX - meanScreenX;
    const double sy = points[i].screenY - meanScreenY;

    rawX2 += rx * rx;
    rawXY += rx * ry;
    rawY2 += ry * ry;
    rawXScreenX += rx * sx;
    rawYScreenX += ry * sx;
    rawXScreenY += rx * sy;
    rawYScreenY += ry * sy;
  }

  const double det = rawX2 * rawY2 - rawXY * rawXY;
  if (std::fabs(det) < 1e-9) {
    return cal;
  }

  const double a = (rawXScreenX * rawY2 - rawXY * rawYScreenX) / det;
  const double b = (rawX2 * rawYScreenX - rawXY * rawXScreenX) / det;
  const double d = (rawXScreenY * rawY2 - rawXY * rawYScreenY) / det;
  const double e = (rawX2 * rawYScreenY - rawXY * rawXScreenY) / det;

  cal.a = static_cast<int32_t>(std::llround(a * 65536.0));
  cal.b = static_cast<int32_t>(std::llround(b * 65536.0));
  cal.c = static_cast<int32_t>(std::llround(meanScreenX - a * meanRawX - b * meanRawY));
  cal.d = static_cast<int32_t>(std::llround(d * 65536.0));
  cal.e = static_cast<int32_t>(std::llround(e * 65536.0));
  cal.f = static_cast<int32_t>(std::llround(meanScreenY - d * meanRawX - e * meanRawY));

  return cal;
}

inline Point applyCalibration(const Calibration& cal, int32_t rawX, int32_t rawY) {
  const int32_t x = (cal.a * rawX + cal.b * rawY) / 65536 + cal.c;
  const int32_t y = (cal.d * rawX + cal.e * rawY) / 65536 + cal.f;

  return Point{detail::clampScreen(x, config::kScreenWidth - 1),
               detail::clampScreen(y, config::kScreenHeight - 1)};
}

}  // namespace paint
