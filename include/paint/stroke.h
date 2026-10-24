#pragma once

// State machine for a single freehand brush/eraser stroke.

#include <cstdint>

#include "paint/brush.h"
#include "paint/canvas.h"
#include "paint/geometry.h"
#include "paint/undo.h"

namespace paint {

class StrokeController {
 public:
  void setCanvas(Canvas* canvas) { canvas_ = canvas; }
  void setUndo(UndoStack* undo) { undo_ = undo; }

  void begin(const BrushMask& mask, uint16_t color, Point p) {
    if (!canvas_ || !canvas_->ready()) {
      return;
    }
    mask_ = &mask;
    color_ = color;
    last_ = p;
    active_ = true;
    stampBrush(*canvas_, *mask_, p.x, p.y, color_, undo_);
  }

  void move(Point p) {
    if (!active_) {
      return;
    }
    strokeBrush(*canvas_, *mask_, last_, p, color_, undo_);
    last_ = p;
  }

  void end(Point p) {
    if (!active_) {
      return;
    }
    move(p);
    active_ = false;
  }

  void cancel() { active_ = false; }
  bool active() const { return active_; }

 private:
  Canvas* canvas_ = nullptr;
  UndoStack* undo_ = nullptr;
  const BrushMask* mask_ = nullptr;
  uint16_t color_ = 0;
  Point last_{};
  bool active_ = false;
};

}  // namespace paint
