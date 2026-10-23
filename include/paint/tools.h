#pragma once

// Tool identifiers and the tool interface (implementation lands in M4/M5).

#include <cstdint>

namespace paint {

enum class ToolId : uint8_t {
  Brush,
  Eraser,
  Line,
  Rectangle,
  Ellipse,
  Fill,
  Eyedropper,
};

}  // namespace paint
