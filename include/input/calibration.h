#pragma once

// 4-point affine touch calibration (implementation lands in M3).
// Coefficients are stored in Q16.16 fixed point.

#include <cstdint>

#include "paint/geometry.h"

namespace paint {

struct Calibration {
  int32_t a = 0, b = 0, c = 0;  // x_screen = a*x_raw + b*y_raw + c
  int32_t d = 0, e = 0, f = 0;  // y_screen = d*x_raw + e*y_raw + f
};

Point applyCalibration(const Calibration& cal, int32_t rawX, int32_t rawY);

}  // namespace paint
