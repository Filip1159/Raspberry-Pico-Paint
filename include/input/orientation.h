#pragma once

// Display/touch orientation helpers.
//
// The panel is mounted in portrait for both supported settings: rotation 2 is
// the normal upright view and rotation 0 is the same portrait image flipped
// 180 degrees. The UI framebuffer is always 320x480; only the panel scan and
// the touch coordinate mapping change.
//
// Touch calibration is stored in the canonical rotation-2 coordinate space.
// These helpers convert between that space and the framebuffer coordinates
// used by the UI when the display is flipped.

#include <cstdint>

#include "config.h"
#include "paint/geometry.h"

namespace paint {

constexpr bool isSupportedRotation(uint8_t rotation) {
  return rotation == 0 || rotation == config::kDisplayRotation;
}

constexpr uint8_t normalizeRotation(uint8_t rotation) {
  return isSupportedRotation(rotation) ? rotation : config::kDisplayRotation;
}

constexpr Point flipPoint(Point p) {
  return Point{static_cast<int16_t>(config::kScreenWidth - 1 - p.x),
               static_cast<int16_t>(config::kScreenHeight - 1 - p.y)};
}

// Canonical (rotation 2) -> framebuffer coordinates for the current display
// rotation. Used for touch samples before they enter the UI.
constexpr Point mapCanonicalToDisplay(Point p, uint8_t rotation) {
  return normalizeRotation(rotation) == 0 ? flipPoint(p) : p;
}

// Framebuffer coordinates -> canonical (rotation 2) coordinates. Used when
// fitting a calibration while the display is flipped.
constexpr Point mapDisplayToCanonical(Point p, uint8_t rotation) {
  return normalizeRotation(rotation) == 0 ? flipPoint(p) : p;
}

}  // namespace paint
