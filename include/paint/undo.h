#pragma once

// Undo/redo bookkeeping (implementation lands in M4).
// Tile snapshots for normal strokes, RLE snapshots for fill/clear.

#include <cstdint>

namespace paint {

class UndoStack {
 public:
  void beginAction();
  void endAction();
  bool undo();
  bool redo();
  void clear();
};

}  // namespace paint
