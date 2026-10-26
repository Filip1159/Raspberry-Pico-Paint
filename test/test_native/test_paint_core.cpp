#include <unity.h>

#include <cstring>

#include "config.h"
#include "display/dirty.h"
#include "input/calibration.h"
#include "input/orientation.h"
#include "input/touch_state.h"
#include "paint/brush.h"
#include "paint/canvas.h"
#include "paint/codec_rle.h"
#include "paint/color.h"
#include "paint/crc32.h"
#include "paint/geometry.h"
#include "paint/pnt.h"
#include "paint/shapes.h"
#include "paint/stroke.h"
#include "paint/undo.h"
#include "storage/autosave.h"
#include "storage/boot_health.h"
#include "storage/paint_files.h"
#include "storage/storage.h"
#include "storage/settings_codec.h"
#include "ui/font5x7.h"
#include "ui/idle.h"
#include "ui/ui.h"

using namespace paint;

void setUp() {}
void tearDown() {}

static void test_rgb565_primaries() {
  TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(255, 0, 0));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0, 255, 0));
  TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0, 0, 255));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(255, 255, 255));
  TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565(0, 0, 0));
}

static void test_expand_boundaries() {
  TEST_ASSERT_EQUAL_UINT8(0, expand5(0));
  TEST_ASSERT_EQUAL_UINT8(255, expand5(31));
  TEST_ASSERT_EQUAL_UINT8(0, expand6(0));
  TEST_ASSERT_EQUAL_UINT8(255, expand6(63));
}

static void test_rgb565_to_rgb888() {
  TEST_ASSERT_EQUAL_HEX32(0x000000, rgb565_to_rgb888(0x0000));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, rgb565_to_rgb888(0xFFFF));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, rgb565_to_rgb888(0xF800));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, rgb565_to_rgb888(0x07E0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, rgb565_to_rgb888(0x001F));
}

static void test_rgb666_channels() {
  TEST_ASSERT_EQUAL_UINT8(63, red6(0xF800));
  TEST_ASSERT_EQUAL_UINT8(63, green6_from_565(0x07E0));
  TEST_ASSERT_EQUAL_UINT8(63, blue6(0x001F));
  TEST_ASSERT_EQUAL_UINT8(0, red6(0x0000));
}

static void test_geometry_contains() {
  const Rect r = makeRect(10, 20, 30, 40);
  TEST_ASSERT_TRUE(r.contains(10, 20));
  TEST_ASSERT_TRUE(r.contains(39, 59));
  TEST_ASSERT_FALSE(r.contains(40, 59));
  TEST_ASSERT_FALSE(r.contains(39, 60));
  TEST_ASSERT_FALSE(r.empty());
}

static void test_geometry_intersect() {
  const Rect a = makeRect(0, 0, 100, 100);
  const Rect b = makeRect(50, 50, 100, 100);
  const Rect c = a.intersect(b);
  TEST_ASSERT_TRUE(c == makeRect(50, 50, 50, 50));

  const Rect d = a.intersect(makeRect(200, 200, 10, 10));
  TEST_ASSERT_TRUE(d.empty());
}

static void test_canvas_dimensions() {
  TEST_ASSERT_EQUAL_INT(320, config::kCanvasWidth);
  TEST_ASSERT_EQUAL_INT(432, config::kCanvasHeight);
  TEST_ASSERT_EQUAL_INT(320, config::kScreenWidth);
  TEST_ASSERT_EQUAL_INT(480, config::kScreenHeight);
}

static void test_dirty_region_merges_and_clears() {
  DirtyRegion dirty;
  TEST_ASSERT_TRUE(dirty.empty());
  TEST_ASSERT_TRUE(dirty.bounds().empty());

  dirty.add(makeRect(10, 20, 30, 40));
  dirty.add(makeRect(50, 60, 10, 10));
  TEST_ASSERT_TRUE(dirty.bounds() == makeRect(10, 20, 50, 50));

  dirty.add(Rect{});
  TEST_ASSERT_TRUE(dirty.bounds() == makeRect(10, 20, 50, 50));

  const Rect taken = dirty.take();
  TEST_ASSERT_TRUE(taken == makeRect(10, 20, 50, 50));
  TEST_ASSERT_TRUE(dirty.empty());
}

static void test_dirty_region_keeps_disjoint_rects() {
  DirtyRegion dirty;
  dirty.add(makeRect(0, 0, 10, 10));
  dirty.add(makeRect(100, 100, 10, 10));

  TEST_ASSERT_EQUAL_INT(2, dirty.count());
  TEST_ASSERT_TRUE(dirty.rect(0) == makeRect(0, 0, 10, 10));
  TEST_ASSERT_TRUE(dirty.rect(1) == makeRect(100, 100, 10, 10));
  TEST_ASSERT_TRUE(dirty.bounds() == makeRect(0, 0, 110, 110));

  dirty.add(makeRect(5, 5, 10, 10));
  TEST_ASSERT_EQUAL_INT(2, dirty.count());
  TEST_ASSERT_TRUE(dirty.bounds() == makeRect(0, 0, 110, 110));

  dirty.add(makeRect(0, 0, 320, 480));
  TEST_ASSERT_EQUAL_INT(1, dirty.count());
  TEST_ASSERT_TRUE(dirty.rect(0) == makeRect(0, 0, 320, 480));
}

static uint16_t testFramebuffer[config::kScreenWidth * config::kScreenHeight];

static Canvas testCanvas() {
  Canvas canvas(testFramebuffer, config::kScreenWidth, ui_layout::kCanvas);
  return canvas;
}

static void test_canvas_clips_to_bounds_and_tracks_dirty() {
  Canvas canvas = testCanvas();
  for (int i = 0; i < config::kScreenWidth * config::kScreenHeight; ++i) {
    testFramebuffer[i] = 0;
  }

  canvas.clear(config::kPaperColor);
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, testFramebuffer[48 * config::kScreenWidth]);
  TEST_ASSERT_EQUAL_HEX16(0, testFramebuffer[47 * config::kScreenWidth]);
  TEST_ASSERT_TRUE(canvas.dirty());
  TEST_ASSERT_TRUE(canvas.dirtyRect(0) == ui_layout::kCanvas);

  canvas.clearDirty();
  canvas.setPixel(0, 47, kRed);
  TEST_ASSERT_EQUAL_HEX16(0, testFramebuffer[47 * config::kScreenWidth]);
  TEST_ASSERT_FALSE(canvas.dirty());

  canvas.setPixel(10, 50, kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(10, 50));
  TEST_ASSERT_TRUE(canvas.dirty());
  TEST_ASSERT_TRUE(canvas.dirtyRect(0) == makeRect(10, 50, 1, 1));

  canvas.clearDirty();
  canvas.fillRect(makeRect(-10, 40, 30, 20), kBlue);
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(0, 48));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(19, 59));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(20, 59));
  TEST_ASSERT_TRUE(canvas.dirtyRect(0) == makeRect(0, 48, 20, 12));
}

static void test_brush_masks_are_round_and_symmetric() {
  const int sizes[] = {1, 2, 4, 8, 16, 24, 32};
  int previousCount = 0;

  for (int size : sizes) {
    const BrushMask& mask = brushMaskForSize(size);
    TEST_ASSERT_EQUAL_INT(size, mask.diameter);
    TEST_ASSERT_TRUE(mask.coverage != nullptr);
    TEST_ASSERT_EQUAL_INT((size + 7) / 8, mask.stride);

    int count = 0;
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const bool on = mask.test(x, y);
        TEST_ASSERT_EQUAL_INT(on, mask.test(size - 1 - x, y));
        TEST_ASSERT_EQUAL_INT(on, mask.test(x, size - 1 - y));
        if (on) {
          ++count;
        }
      }
    }
    TEST_ASSERT_TRUE(count > previousCount);
    TEST_ASSERT_TRUE(count <= size * size);
    previousCount = count;
  }

  TEST_ASSERT_TRUE(brushMaskForSize(1).test(0, 0));
  TEST_ASSERT_TRUE(brushMaskForSize(8).test(3, 3));
  TEST_ASSERT_TRUE(brushMaskForSize(8).test(4, 4));
  TEST_ASSERT_FALSE(brushMaskForSize(8).test(0, 0));
  TEST_ASSERT_EQUAL_INT(32, brushMaskForSize(100).diameter);
}

static void test_brush_stamp_clips_and_marks_dirty() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  const BrushMask& mask = brushMaskForSize(8);
  stampBrush(canvas, mask, 100, 100, kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(100, 100));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(96, 96));
  TEST_ASSERT_TRUE(canvas.dirtyRect(0) == makeRect(96, 96, 8, 8));

  canvas.clearDirty();
  stampBrush(canvas, mask, 0, 48, kBlue);
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(0, 48));
  TEST_ASSERT_TRUE(canvas.dirtyRect(0) == makeRect(0, 48, 4, 4));

  canvas.clearDirty();
  stampBrush(canvas, mask, 40, 20, kGreen);
  TEST_ASSERT_FALSE(canvas.dirty());

  canvas.clearDirty();
  stampBrush(canvas, mask, 319, 479, kGreen);
  TEST_ASSERT_EQUAL_HEX16(kGreen, canvas.pixel(319, 479));
  TEST_ASSERT_TRUE(canvas.dirtyRect(0) == makeRect(315, 475, 5, 5));
}

static void test_stroke_is_continuous_and_clipped() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  const BrushMask& thin = brushMaskForSize(1);
  strokeBrush(canvas, thin, {10, 60}, {30, 70}, kBlack);
  TEST_ASSERT_EQUAL_HEX16(kBlack, canvas.pixel(10, 60));
  TEST_ASSERT_EQUAL_HEX16(kBlack, canvas.pixel(30, 70));

  int marked = 0;
  for (int x = 10; x <= 30; ++x) {
    bool found = false;
    for (int y = 60; y <= 70; ++y) {
      if (canvas.pixel(x, y) == kBlack) {
        found = true;
        ++marked;
      }
    }
    TEST_ASSERT_TRUE(found);
  }
  TEST_ASSERT_TRUE(marked >= 21);

  canvas.clearDirty();
  const BrushMask& thick = brushMaskForSize(8);
  strokeBrush(canvas, thick, {50, 40}, {50, 100}, kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(50, 48));
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(50, 100));

  canvas.clearDirty();
  strokeBrush(canvas, thick, {310, 470}, {330, 490}, kBlue);
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(319, 479));
  TEST_ASSERT_TRUE(canvas.dirty());
  TEST_ASSERT_TRUE(canvas.dirtyRect(0).contains(319, 479));
}

static void test_stroke_controller_eraser_and_cancel() {
  Canvas canvas = testCanvas();
  canvas.clear(kRed);
  canvas.clearDirty();

  StrokeController stroke;
  stroke.setCanvas(&canvas);
  const BrushMask& mask = brushMaskForSize(16);
  stroke.begin(mask, config::kPaperColor, {80, 120});
  TEST_ASSERT_TRUE(stroke.active());
  stroke.move({120, 120});
  stroke.end({140, 140});
  TEST_ASSERT_FALSE(stroke.active());
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(80, 120));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(120, 120));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(140, 140));

  canvas.clearDirty();
  stroke.setCanvas(&canvas);
  stroke.begin(mask, kBlue, {200, 200});
  stroke.move({220, 200});
  stroke.cancel();
  TEST_ASSERT_FALSE(stroke.active());
  stroke.move({240, 200});
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(240, 200));
}


static void test_undo_redo_round_trip() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  UndoStack undo;
  StrokeController stroke;
  stroke.setCanvas(&canvas);
  stroke.setUndo(&undo);

  undo.beginAction();
  const BrushMask& mask = brushMaskForSize(8);
  stroke.begin(mask, kBlue, {100, 100});
  stroke.move({140, 120});
  stroke.end({160, 130});
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_TRUE(undo.canUndo());
  TEST_ASSERT_FALSE(undo.canRedo());
  TEST_ASSERT_EQUAL_INT(1, undo.undoDepth());
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(100, 100));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(140, 120));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(160, 130));
  const UndoStack::Usage usage = undo.usage();
  TEST_ASSERT_EQUAL_INT(1, usage.actions);
  TEST_ASSERT_TRUE(usage.records > 0);
  TEST_ASSERT_TRUE(usage.records <= UndoStack::kMaxTileRecords);
  TEST_ASSERT_TRUE(usage.bytes > 0);

  canvas.clearDirty();
  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_FALSE(undo.canUndo());
  TEST_ASSERT_TRUE(undo.canRedo());
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(100, 100));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(140, 120));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(160, 130));
  TEST_ASSERT_TRUE(canvas.dirty());

  TEST_ASSERT_TRUE(undo.redo(canvas));
  TEST_ASSERT_TRUE(undo.canUndo());
  TEST_ASSERT_FALSE(undo.canRedo());
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(100, 100));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(160, 130));

  undo.clear();
  const UndoStack::Usage cleared = undo.usage();
  TEST_ASSERT_EQUAL_INT(0, cleared.actions);
  TEST_ASSERT_EQUAL_INT(0, cleared.records);
  TEST_ASSERT_EQUAL_INT(0, cleared.bytes);
}

static void test_shape_preview_cancel_leaves_no_residue() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  ShapePreview preview;
  preview.update(canvas, ShapeKind::Line, {20, 100}, {200, 300}, kBlue);
  TEST_ASSERT_TRUE(preview.active());
  TEST_ASSERT_TRUE(canvas.pixel(20, 100) != config::kPaperColor);
  TEST_ASSERT_TRUE(canvas.pixel(200, 300) != config::kPaperColor);

  preview.update(canvas, ShapeKind::Rect, {20, 100}, {200, 300}, kBlue);
  TEST_ASSERT_TRUE(canvas.pixel(20, 100) != config::kPaperColor);
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(110, 200));

  preview.cancel(canvas);
  TEST_ASSERT_FALSE(preview.active());
  for (int y = 96; y <= 304; ++y) {
    for (int x = 16; x <= 204; ++x) {
      TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(x, y));
    }
  }
}

static void test_shape_filled_commit_and_undo() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  UndoStack undo;
  ShapePreview preview;

  // Filled previews show only the boundary, so the interior stays untouched
  // until commit and cancel never has a large region to restore.
  preview.update(canvas, ShapeKind::Rect, {40, 100}, {120, 160}, kBlue, true);
  TEST_ASSERT_TRUE(preview.active());
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(80, 130));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(40, 130));
  preview.cancel(canvas);
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(40, 130));

  preview.commit(canvas, undo, ShapeKind::Rect, {40, 100}, {120, 160}, kBlue,
                 true);
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(80, 130));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(40, 130));
  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(80, 130));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(40, 130));

  preview.commit(canvas, undo, ShapeKind::Ellipse, {40, 100}, {120, 160}, kRed,
                 true);
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(80, 130));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(42, 102));
  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(80, 130));
}

static void test_undo_long_action_tiles() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  TEST_ASSERT_TRUE(UndoStack::kMaxTilesPerAction >= 48);

  UndoStack undo;
  undo.beginAction();
  constexpr int kTiles = 40;
  for (int i = 0; i < kTiles; ++i) {
    const int col = i % UndoStack::kTilesX;
    const int row = i / UndoStack::kTilesX;
    const int16_t x = static_cast<int16_t>(col * config::kTileSize + 16);
    const int16_t y = static_cast<int16_t>(
        ui_layout::kCanvas.y + row * config::kTileSize + 16);
    undo.captureRect(canvas, {x, y, 1, 1});
    canvas.setPixel(x, y, kBlue);
  }
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_EQUAL_INT(kTiles, undo.usage().records);

  TEST_ASSERT_TRUE(undo.undo(canvas));
  for (int i = 0; i < kTiles; ++i) {
    const int col = i % UndoStack::kTilesX;
    const int row = i / UndoStack::kTilesX;
    const int x = col * config::kTileSize + 16;
    const int y = ui_layout::kCanvas.y + row * config::kTileSize + 16;
    TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(x, y));
  }

  // One action cannot exceed the per-action cap even though the pool has a
  // few more records; the overflow flag drives the user-facing toast.
  undo.beginAction();
  for (int i = 0; i <= UndoStack::kMaxTilesPerAction; ++i) {
    const int col = i % UndoStack::kTilesX;
    const int row = i / UndoStack::kTilesX;
    const int16_t x = static_cast<int16_t>(col * config::kTileSize + 16);
    const int16_t y = static_cast<int16_t>(
        ui_layout::kCanvas.y + row * config::kTileSize + 16);
    undo.captureRect(canvas, {x, y, 1, 1});
    canvas.setPixel(x, y, kRed);
  }
  TEST_ASSERT_FALSE(undo.commit());
  TEST_ASSERT_TRUE(undo.takeOverflow());
}

static void test_shape_commit_rect_and_ellipse_undo() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  UndoStack undo;
  ShapePreview preview;
  preview.update(canvas, ShapeKind::Rect, {40, 100}, {120, 160}, kBlue);
  preview.commit(canvas, undo, ShapeKind::Rect, {40, 100}, {120, 160}, kBlue);
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_FALSE(preview.active());
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(40, 100));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(120, 160));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(40, 130));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(80, 130));

  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(40, 100));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(40, 130));

  preview.commit(canvas, undo, ShapeKind::Ellipse, {40, 100}, {120, 160}, kRed);
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(40, 130));
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(120, 130));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(80, 130));
  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(40, 130));
}

static void test_flood_fill_stays_inside_outline() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  UndoStack undo;
  ShapePreview preview;
  preview.commit(canvas, undo, ShapeKind::Rect, {40, 100}, {120, 160}, kBlue);
  TEST_ASSERT_TRUE(undo.commit());

  undo.beginAction();
  TEST_ASSERT_TRUE(floodFill(canvas, &undo, {80, 130}, kRed));
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(80, 130));
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(41, 101));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(40, 130));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(10, 130));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(130, 130));

  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(80, 130));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(40, 130));
}

static void test_flood_fill_same_colour_is_noop() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();
  TEST_ASSERT_TRUE(floodFill(canvas, nullptr, {80, 130}, config::kPaperColor));
  TEST_ASSERT_FALSE(canvas.dirty());
}

static void test_undo_multiple_actions_and_new_action_discards_redo() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  UndoStack undo;
  StrokeController stroke;
  stroke.setCanvas(&canvas);
  stroke.setUndo(&undo);
  const BrushMask& mask = brushMaskForSize(16);

  for (int i = 0; i < 3; ++i) {
    undo.beginAction();
    const int y = 100 + i * 80;
    stroke.begin(mask, kRed, {80, static_cast<int16_t>(y)});
    stroke.end({120, static_cast<int16_t>(y)});
    TEST_ASSERT_TRUE(undo.commit());
  }
  TEST_ASSERT_EQUAL_INT(3, undo.undoDepth());

  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(100, 260));
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(100, 180));
  TEST_ASSERT_EQUAL_INT(2, undo.undoDepth());
  TEST_ASSERT_EQUAL_INT(1, undo.redoDepth());

  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(100, 180));
  TEST_ASSERT_EQUAL_INT(1, undo.undoDepth());
  TEST_ASSERT_EQUAL_INT(2, undo.redoDepth());

  TEST_ASSERT_TRUE(undo.redo(canvas));
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(100, 180));
  TEST_ASSERT_TRUE(undo.redo(canvas));
  TEST_ASSERT_EQUAL_HEX16(kRed, canvas.pixel(100, 260));
  TEST_ASSERT_FALSE(undo.redo(canvas));

  TEST_ASSERT_TRUE(undo.undo(canvas));
  undo.beginAction();
  stroke.begin(mask, kGreen, {200, 100});
  stroke.end({240, 100});
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_FALSE(undo.canRedo());
  TEST_ASSERT_EQUAL_INT(3, undo.undoDepth());
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(100, 260));
}

static void test_undo_compressed_deltas_keep_ten_actions() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  UndoStack undo;
  StrokeController stroke;
  stroke.setCanvas(&canvas);
  stroke.setUndo(&undo);
  const BrushMask& mask = brushMaskForSize(8);

  // Each stroke spans four 32x32 tiles. Ten such actions need 40 tile
  // records, more than the old 30-record raw pool could hold.
  for (int i = 0; i < 10; ++i) {
    undo.beginAction();
    const int y = 64 + i * 36;
    stroke.begin(mask, kBlue, {30, static_cast<int16_t>(y)});
    stroke.end({110, static_cast<int16_t>(y)});
    TEST_ASSERT_TRUE(undo.commit());
  }
  TEST_ASSERT_EQUAL_INT(10, undo.undoDepth());

  for (int i = 0; i < 10; ++i) {
    TEST_ASSERT_TRUE(undo.undo(canvas));
  }
  TEST_ASSERT_FALSE(undo.canUndo());
  TEST_ASSERT_EQUAL_INT(10, undo.redoDepth());
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(70, 64 + 5 * 36));

  for (int i = 0; i < 10; ++i) {
    TEST_ASSERT_TRUE(undo.redo(canvas));
  }
  TEST_ASSERT_TRUE(undo.canUndo());
  TEST_ASSERT_FALSE(undo.canRedo());
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(70, 64 + 5 * 36));
}

static void test_undo_raw_tile_fallback() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  for (int y = 64; y < 96; ++y) {
    for (int x = 0; x < 32; ++x) {
      canvas.setPixel(x, y, ((x + y) & 1) ? kRed : kGreen);
    }
  }
  canvas.clearDirty();
  const uint16_t before = canvas.pixel(10, 70);

  UndoStack undo;
  StrokeController stroke;
  stroke.setCanvas(&canvas);
  stroke.setUndo(&undo);
  const BrushMask& mask = brushMaskForSize(8);

  undo.beginAction();
  stroke.begin(mask, kBlue, {6, 72});
  stroke.end({26, 72});
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(10, 70));

  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(before, canvas.pixel(10, 70));
  TEST_ASSERT_TRUE(undo.redo(canvas));
  TEST_ASSERT_EQUAL_HEX16(kBlue, canvas.pixel(10, 70));
}

static void test_undo_handles_canvas_bottom_and_clear() {
  Canvas canvas = testCanvas();
  canvas.clear(config::kPaperColor);
  canvas.clearDirty();

  UndoStack undo;
  StrokeController stroke;
  stroke.setCanvas(&canvas);
  stroke.setUndo(&undo);
  const BrushMask& mask = brushMaskForSize(24);

  undo.beginAction();
  stroke.begin(mask, kGreen, {160, 470});
  stroke.end({170, 478});
  TEST_ASSERT_TRUE(undo.commit());
  TEST_ASSERT_EQUAL_HEX16(kGreen, canvas.pixel(160, 470));

  TEST_ASSERT_TRUE(undo.undo(canvas));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(160, 470));
  TEST_ASSERT_EQUAL_HEX16(config::kPaperColor, canvas.pixel(170, 478));

  undo.clear();
  TEST_ASSERT_FALSE(undo.canUndo());
  TEST_ASSERT_FALSE(undo.canRedo());
}


static void test_hsv_color_round_trip() {
  TEST_ASSERT_EQUAL_HEX16(kRed, hsvToRgb565(0, 255, 255));
  TEST_ASSERT_EQUAL_HEX16(kGreen, hsvToRgb565(85, 255, 255));
  TEST_ASSERT_EQUAL_HEX16(kBlue, hsvToRgb565(171, 255, 255));
  TEST_ASSERT_EQUAL_HEX16(kBlack, hsvToRgb565(0, 0, 0));
  TEST_ASSERT_EQUAL_HEX16(kWhite, hsvToRgb565(0, 0, 255));

  const Hsv red = rgb565ToHsv(kRed);
  TEST_ASSERT_UINT8_WITHIN(2, 0, red.h);
  TEST_ASSERT_EQUAL_UINT8(255, red.s);
  TEST_ASSERT_EQUAL_UINT8(255, red.v);

  const Hsv green = rgb565ToHsv(kGreen);
  TEST_ASSERT_UINT8_WITHIN(2, 85, green.h);
  TEST_ASSERT_EQUAL_UINT8(255, green.s);
  TEST_ASSERT_EQUAL_UINT8(255, green.v);

  const Hsv blue = rgb565ToHsv(kBlue);
  TEST_ASSERT_UINT8_WITHIN(2, 171, blue.h);
  TEST_ASSERT_EQUAL_UINT8(255, blue.s);
  TEST_ASSERT_EQUAL_UINT8(255, blue.v);

  const Hsv gray = rgb565ToHsv(rgb565(128, 128, 128));
  TEST_ASSERT_TRUE(gray.s <= 4);
  TEST_ASSERT_UINT8_WITHIN(3, 132, gray.v);
}

static void test_ui_layout_new_sheets_and_recent_colors() {
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight,
                        ui_layout::sheetRect(Sheet::ConfirmNew).bottom());
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight,
                        ui_layout::sheetRect(Sheet::Hsv).bottom());
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::sheetButton(Sheet::ConfirmNew));
  TEST_ASSERT_EQUAL_INT(2, ui_layout::sheetButton(Sheet::Hsv));

  const Rect hsvSheet = ui_layout::sheetRect(Sheet::Hsv);
  const Rect controls[] = {
      ui_layout::kHsvHueBar, ui_layout::kHsvSquare, ui_layout::kHsvPreview,
      ui_layout::kHsvCancel, ui_layout::kHsvOk, ui_layout::kHsvReadout,
      ui_layout::kHsvHuePaint, ui_layout::kHsvSquarePaint,
  };
  for (const Rect& r : controls) {
    TEST_ASSERT_TRUE(hsvSheet.contains({r.x, r.y}));
    TEST_ASSERT_TRUE(hsvSheet.contains(
        {static_cast<int16_t>(r.right() - 1),
         static_cast<int16_t>(r.bottom() - 1)}));
  }
  TEST_ASSERT_TRUE(ui_layout::kHsvReadout.intersect(ui_layout::kHsvPreview).empty());
  TEST_ASSERT_TRUE(ui_layout::kHsvHuePaint.contains(
      {static_cast<int16_t>(ui_layout::kHsvHueBar.x),
       static_cast<int16_t>(ui_layout::kHsvHueBar.y - 3)}));
  TEST_ASSERT_TRUE(ui_layout::kHsvSquarePaint.contains(
      {static_cast<int16_t>(ui_layout::kHsvSquare.x),
       static_cast<int16_t>(ui_layout::kHsvSquare.y)}));
  TEST_ASSERT_TRUE(ui_layout::kHsvSquare.intersect(ui_layout::kHsvOk).empty());
  TEST_ASSERT_TRUE(ui_layout::kHsvSquare.bottom() <= ui_layout::kHsvCancel.y);

  for (int i = 0; i < ui_layout::kRecentColorCount; ++i) {
    const Rect r = ui_layout::paletteRecentRect(i);
    const Rect sheet = ui_layout::sheetRect(Sheet::Palette);
    TEST_ASSERT_TRUE(sheet.contains({r.x, r.y}));
    TEST_ASSERT_TRUE(sheet.contains(
        {static_cast<int16_t>(r.right() - 1),
         static_cast<int16_t>(r.bottom() - 1)}));
    TEST_ASSERT_TRUE(r.intersect(ui_layout::kPaletteHsvButton).empty());
    TEST_ASSERT_EQUAL_INT(i, ui_layout::hitPaletteRecent(
                                 {static_cast<int16_t>(r.x + 1),
                                  static_cast<int16_t>(r.y + 1)}));
  }
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitPaletteRecent({123, 280}));
}

static void uiTap(Ui& ui, int x, int y, uint32_t nowMs) {
  TouchFrame down;
  down.pressed = true;
  down.down = true;
  down.x = static_cast<int16_t>(x);
  down.y = static_cast<int16_t>(y);
  down.startX = down.x;
  down.startY = down.y;
  ui.update(down, nowMs);

  TouchFrame up;
  up.up = true;
  up.tap = true;
  up.x = down.x;
  up.y = down.y;
  up.startX = down.x;
  up.startY = down.y;
  up.durationMs = 40;
  ui.update(up, nowMs + 40);
}

static void test_ui_new_canvas_confirmation_flow() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(ui.screen()));

  uiTap(ui, 280, 24, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Menu),
                        static_cast<int>(ui.sheet()));

  uiTap(ui, 160, 198, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::ConfirmNew),
                        static_cast<int>(ui.sheet()));

  uiTap(ui, 87, 426, 1400);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_FALSE(ui.takeNewCanvasRequest());

  uiTap(ui, 280, 24, 1600);
  uiTap(ui, 160, 198, 1800);
  uiTap(ui, 233, 426, 2000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_TRUE(ui.takeNewCanvasRequest());
  TEST_ASSERT_FALSE(ui.takeNewCanvasRequest());
}

static void uiLongPress(Ui& ui, int x, int y, uint32_t nowMs) {
  TouchFrame down;
  down.pressed = true;
  down.down = true;
  down.x = static_cast<int16_t>(x);
  down.y = static_cast<int16_t>(y);
  down.startX = down.x;
  down.startY = down.y;
  ui.update(down, nowMs);

  TouchFrame hold = down;
  hold.down = false;
  hold.longPress = true;
  ui.update(hold, nowMs + 700);

  TouchFrame up;
  up.up = true;
  up.x = down.x;
  up.y = down.y;
  up.startX = down.x;
  up.startY = down.y;
  up.durationMs = 740;
  ui.update(up, nowMs + 740);
}

static void test_ui_save_and_save_as_flow() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  // Save with no current document asks the main loop for a free name.
  uiTap(ui, 280, 24, 1000);
  uiTap(ui, 160, 238, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_TRUE(ui.takeSaveAsRequest());
  TEST_ASSERT_FALSE(ui.takeSaveAsRequest());

  ui.promptSaveAs("img0007.pnt");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::SaveAs),
                        static_cast<int>(ui.sheet()));
  uiTap(ui, 233, 426, 1400);

  char name[kPaintFileNameSize];
  TEST_ASSERT_TRUE(ui.takeSaveRequest(name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("img0007.pnt", name);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));

  // Once the document has a name, Save writes it directly.
  ui.setDocument("img0007.pnt", false);
  uiTap(ui, 280, 24, 1600);
  uiTap(ui, 160, 238, 1800);
  TEST_ASSERT_TRUE(ui.takeSaveRequest(name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("img0007.pnt", name);
  TEST_ASSERT_FALSE(ui.takeSaveAsRequest());

  // Save as always asks for a new name.
  uiTap(ui, 280, 24, 2000);
  uiTap(ui, 160, 278, 2200);
  TEST_ASSERT_TRUE(ui.takeSaveAsRequest());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));
}

static void test_ui_gallery_load_delete_and_paging() {
  static const char* const kNames[5] = {
      "img0001.pnt", "img0002.pnt", "img0003.pnt", "img0004.pnt",
      "img0005.pnt",
  };

  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);
  ui.setGalleryFiles(kNames, 5);
  TEST_ASSERT_EQUAL_INT(5, ui.galleryCount());
  TEST_ASSERT_EQUAL_INT(2, ui.galleryPageCount());
  ui.setDocument("img0001.pnt", true);

  uiTap(ui, 280, 24, 1000);
  uiTap(ui, 160, 318, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Gallery),
                        static_cast<int>(ui.screen()));
  TEST_ASSERT_EQUAL_INT(0, ui.galleryPage());

  // A tap asks for confirmation before discarding the canvas.
  uiTap(ui, 85, 160, 1400);
  TEST_ASSERT_TRUE(ui.galleryDialogOpen());
  TEST_ASSERT_FALSE(ui.takeLoadRequest(nullptr, 0));
  uiTap(ui, 87, 390, 1600);
  TEST_ASSERT_FALSE(ui.galleryDialogOpen());

  uiTap(ui, 85, 160, 1800);
  uiTap(ui, 233, 390, 2000);
  char name[kPaintFileNameSize];
  TEST_ASSERT_TRUE(ui.takeLoadRequest(name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("img0001.pnt", name);
  TEST_ASSERT_FALSE(ui.galleryDialogOpen());

  // Long press opens the destructive confirmation; cancel keeps the file.
  uiLongPress(ui, 235, 160, 2200);
  TEST_ASSERT_TRUE(ui.galleryDialogOpen());
  uiTap(ui, 87, 390, 3000);
  TEST_ASSERT_FALSE(ui.galleryDialogOpen());
  TEST_ASSERT_FALSE(ui.takeDeleteRequest(name, sizeof(name)));

  uiLongPress(ui, 235, 160, 3200);
  uiTap(ui, 233, 390, 4000);
  TEST_ASSERT_TRUE(ui.takeDeleteRequest(name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("img0002.pnt", name);

  // Five files need two pages.
  uiTap(ui, 278, 454, 4200);
  TEST_ASSERT_EQUAL_INT(1, ui.galleryPage());
  uiTap(ui, 42, 454, 4400);
  TEST_ASSERT_EQUAL_INT(0, ui.galleryPage());

  uiTap(ui, 40, 24, 4600);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(ui.screen()));
}

static void test_ui_storage_details_screen() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  uiTap(ui, 280, 24, 1000);
  uiTap(ui, 160, 358, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Settings),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kStorageDetailsButton.x +
                 ui_layout::kStorageDetailsButton.w / 2,
        ui_layout::kStorageDetailsButton.y +
            ui_layout::kStorageDetailsButton.h / 2,
        1400);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Storage),
                        static_cast<int>(ui.screen()));

  uiTap(ui, 40, 24, 1600);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Settings),
                        static_cast<int>(ui.screen()));

  uiTap(ui, 40, 24, 1800);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(ui.screen()));
}

static void test_ui_network_screen_flow() {
  TEST_ASSERT_TRUE(ui_layout::kNetworkInfoArea.bottom() <
                   ui_layout::kNetworkToggleButton.y);
  TEST_ASSERT_TRUE(ui_layout::kNetworkToggleButton.bottom() <=
                   config::kScreenHeight);
  TEST_ASSERT_TRUE(ui_layout::kOtaPanel.bottom() <
                   ui_layout::kNetworkToggleButton.y);
  TEST_ASSERT_TRUE(ui_layout::kOtaProgressBar.bottom() <=
                   ui_layout::kOtaPanel.bottom());
  TEST_ASSERT_TRUE(ui_layout::kScreen.contains(
      {ui_layout::kNetworkToggleButton.x,
       static_cast<int16_t>(ui_layout::kNetworkToggleButton.y + 1)}));

  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  uiTap(ui, 280, 24, 1000);
  uiTap(ui, 160, 398, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Network),
                        static_cast<int>(ui.screen()));
  TEST_ASSERT_FALSE(ui.takeNetworkToggleRequest());

  NetInfo info;
  info.enabled = true;
  info.configured = false;
  info.state = NetState::Disabled;
  ui.setNetworkInfo(info);
  uiTap(ui, ui_layout::kNetworkToggleButton.x +
                 ui_layout::kNetworkToggleButton.w / 2,
        ui_layout::kNetworkToggleButton.y +
            ui_layout::kNetworkToggleButton.h / 2,
        1400);
  TEST_ASSERT_FALSE(ui.takeNetworkToggleRequest());

  info.configured = true;
  info.state = NetState::Connected;
  info.server = true;
  info.ip[0] = 192;
  info.ip[1] = 168;
  info.ip[2] = 1;
  info.ip[3] = 42;
  info.rssi = -55;
  ui.setNetworkInfo(info);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(NetState::Connected),
                        static_cast<int>(ui.networkInfo().state));
  TEST_ASSERT_TRUE(ui.networkInfo().hasIp());
  uiTap(ui, ui_layout::kNetworkToggleButton.x +
                 ui_layout::kNetworkToggleButton.w / 2,
        ui_layout::kNetworkToggleButton.y +
            ui_layout::kNetworkToggleButton.h / 2,
        1600);
  TEST_ASSERT_TRUE(ui.takeNetworkToggleRequest());
  TEST_ASSERT_FALSE(ui.takeNetworkToggleRequest());

  info.enabled = false;
  info.state = NetState::Offline;
  info.lowStorage = true;
  info.server = false;
  ui.setNetworkInfo(info);
  uiTap(ui, ui_layout::kNetworkToggleButton.x +
                 ui_layout::kNetworkToggleButton.w / 2,
        ui_layout::kNetworkToggleButton.y +
            ui_layout::kNetworkToggleButton.h / 2,
        1800);
  TEST_ASSERT_FALSE(ui.takeNetworkToggleRequest());

  ui.beginOta();
  TEST_ASSERT_TRUE(ui.otaActive());
  TEST_ASSERT_TRUE(ui.setOtaProgress(10));
  TEST_ASSERT_FALSE(ui.setOtaProgress(10));
  TEST_ASSERT_TRUE(ui.setOtaProgress(100));
  ui.endOta();
  TEST_ASSERT_FALSE(ui.otaActive());

  uiTap(ui, 40, 24, 2000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(ui.screen()));
}

static void test_ui_gallery_empty_and_file_names() {
  char name[kPaintFileNameSize];
  char path[32];
  TEST_ASSERT_TRUE(paintFileNameForNumber(1, name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("img0001.pnt", name);
  TEST_ASSERT_TRUE(paintFileNameForNumber(9999, name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("img9999.pnt", name);
  TEST_ASSERT_FALSE(paintFileNameForNumber(0, name, sizeof(name)));
  TEST_ASSERT_FALSE(paintFileNameForNumber(10000, name, sizeof(name)));
  TEST_ASSERT_FALSE(paintFileNameForNumber(1, name, 8));

  TEST_ASSERT_TRUE(isPntFileName("img0001.pnt"));
  TEST_ASSERT_TRUE(isPntFileName("sketch.pnt"));
  TEST_ASSERT_TRUE(isAutosaveFileName("autosave.pnt"));
  TEST_ASSERT_FALSE(isAutosaveFileName("img0001.pnt"));
  TEST_ASSERT_EQUAL_STRING("/paint/autosave.pnt", kAutosavePath);
  TEST_ASSERT_FALSE(isPntFileName("img0001.tmp"));
  TEST_ASSERT_FALSE(isPntFileName("/paint/img0001.pnt"));
  TEST_ASSERT_FALSE(isPntFileName(""));

  TEST_ASSERT_EQUAL_INT(42, paintFileNumber("img0042.pnt"));
  TEST_ASSERT_EQUAL_INT(-1, paintFileNumber("img0000.pnt"));
  TEST_ASSERT_EQUAL_INT(-1, paintFileNumber("img00a2.pnt"));
  TEST_ASSERT_EQUAL_INT(-1, paintFileNumber("img0042.tmp"));

  TEST_ASSERT_TRUE(paintFilePath("img0001.pnt", path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("/paint/img0001.pnt", path);
  TEST_ASSERT_FALSE(paintFilePath("a/img0001.pnt", path, sizeof(path)));
  TEST_ASSERT_FALSE(paintFilePath("", path, sizeof(path)));
  TEST_ASSERT_FALSE(paintFilePath("img0001.pnt", path, 8));

  TEST_ASSERT_EQUAL_INT(1, ui_layout::galleryPageCount(0));
  TEST_ASSERT_EQUAL_INT(1, ui_layout::galleryPageCount(4));
  TEST_ASSERT_EQUAL_INT(2, ui_layout::galleryPageCount(5));
  for (int i = 0; i < ui_layout::kGalleryPerPage; ++i) {
    const Rect r = ui_layout::kGalleryItems[i];
    TEST_ASSERT_TRUE(ui_layout::kScreen.contains({r.x, r.y}));
    TEST_ASSERT_TRUE(ui_layout::kScreen.contains(
        {static_cast<int16_t>(r.right() - 1),
         static_cast<int16_t>(r.bottom() - 1)}));
    TEST_ASSERT_EQUAL_INT(i, ui_layout::hitGalleryItem(
                                 {static_cast<int16_t>(r.x + 1),
                                  static_cast<int16_t>(r.y + 1)}));
  }
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitGalleryItem({0, 0}));
  TEST_ASSERT_TRUE(ui_layout::kGalleryPrev.bottom() <=
                   config::kScreenHeight);
  TEST_ASSERT_TRUE(ui_layout::kRecoveryRestore.bottom() <=
                   ui_layout::kRecoveryPanel.bottom());
  TEST_ASSERT_TRUE(ui_layout::kRecoveryDiscard.bottom() <=
                   ui_layout::kRecoveryPanel.bottom());
  TEST_ASSERT_TRUE(ui_layout::kRecoveryRestore.right() <
                   ui_layout::kRecoveryDiscard.x);

  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);
  ui.setGalleryFiles(nullptr, 0);
  uiTap(ui, 280, 24, 1000);
  uiTap(ui, 160, 318, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Gallery),
                        static_cast<int>(ui.screen()));
  TEST_ASSERT_EQUAL_INT(0, ui.galleryCount());
  TEST_ASSERT_EQUAL_INT(1, ui.galleryPageCount());
  uiLongPress(ui, 85, 160, 1400);
  TEST_ASSERT_FALSE(ui.galleryDialogOpen());
}

static void test_autosave_timer_schedule() {
  AutosaveTimer timer;
  TEST_ASSERT_FALSE(timer.pending());
  TEST_ASSERT_FALSE(timer.due(0));

  timer.changed(1000);
  TEST_ASSERT_TRUE(timer.pending());
  TEST_ASSERT_FALSE(timer.due(15999));
  TEST_ASSERT_TRUE(timer.due(16000));

  // Drawing again while armed must not postpone the first deadline.
  timer.changed(12000);
  TEST_ASSERT_FALSE(timer.due(15999));
  TEST_ASSERT_TRUE(timer.due(16000));

  timer.saved();
  TEST_ASSERT_FALSE(timer.pending());
  TEST_ASSERT_FALSE(timer.due(20000));

  timer.changed(17000);
  TEST_ASSERT_FALSE(timer.due(31999));
  TEST_ASSERT_TRUE(timer.due(32000));
  timer.retry(32000);
  TEST_ASSERT_FALSE(timer.due(46999));
  TEST_ASSERT_TRUE(timer.due(47000));

  timer.reset();
  TEST_ASSERT_FALSE(timer.pending());
  TEST_ASSERT_FALSE(timer.due(99999));

  AutosaveTimer wrapped;
  wrapped.changed(0xFFFFFFF0u);
  TEST_ASSERT_FALSE(wrapped.due(14983));
  TEST_ASSERT_TRUE(wrapped.due(14984));
}

static void test_storage_space_and_ota_headroom() {
  constexpr size_t kTwoMb = 2u * 1024u * 1024u;
  StorageStats stats = makeStorageStats(kTwoMb, 512u * 1024u);
  TEST_ASSERT_EQUAL_size_t(kTwoMb, stats.totalBytes);
  TEST_ASSERT_EQUAL_size_t(512u * 1024u, stats.usedBytes);
  TEST_ASSERT_EQUAL_size_t(kTwoMb - 512u * 1024u, stats.freeBytes);
  TEST_ASSERT_EQUAL_UINT8(25, stats.usedPercent());
  TEST_ASSERT_TRUE(stats.otaHeadroom());

  stats = makeStorageStats(kTwoMb, kTwoMb - kOtaHeadroomBytes);
  TEST_ASSERT_EQUAL_size_t(kOtaHeadroomBytes, stats.freeBytes);
  TEST_ASSERT_TRUE(stats.otaHeadroom());

  stats = makeStorageStats(kTwoMb, kTwoMb - kOtaHeadroomBytes + 1);
  TEST_ASSERT_FALSE(stats.otaHeadroom());

  stats = makeStorageStats(1000, 2000);
  TEST_ASSERT_EQUAL_size_t(1000, stats.usedBytes);
  TEST_ASSERT_EQUAL_size_t(0, stats.freeBytes);
  TEST_ASSERT_EQUAL_UINT8(100, stats.usedPercent());
  TEST_ASSERT_FALSE(stats.otaHeadroom());

  stats = makeStorageStats(0, 0);
  TEST_ASSERT_EQUAL_UINT8(0, stats.usedPercent());
  TEST_ASSERT_FALSE(stats.otaHeadroom());

  Ui ui;
  ui.begin(0);
  ui.setStorageStats(makeStorageStats(kTwoMb, kTwoMb - kOtaHeadroomBytes));
  TEST_ASSERT_TRUE(ui.hasOtaHeadroom());
  ui.setStorageStats(makeStorageStats(kTwoMb, kTwoMb - kOtaHeadroomBytes + 1));
  TEST_ASSERT_FALSE(ui.hasOtaHeadroom());
}

static void test_ui_boot_recovery_prompt() {
  Ui restore;
  restore.begin(0);
  restore.setRecoveryAvailable(true);
  restore.update(TouchFrame{}, 900);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Recovery),
                        static_cast<int>(restore.screen()));

  // Recovery is modal until Restore or Discard is tapped.
  uiTap(restore, 40, 24, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Recovery),
                        static_cast<int>(restore.screen()));
  uiTap(restore, 87, 338, 1200);
  TEST_ASSERT_TRUE(restore.takeRecoveryRestoreRequest());
  TEST_ASSERT_FALSE(restore.takeRecoveryRestoreRequest());
  TEST_ASSERT_FALSE(restore.takeRecoveryDiscardRequest());
  restore.showCanvas();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(restore.screen()));

  Ui discard;
  discard.begin(0);
  discard.setRecoveryAvailable(true);
  discard.update(TouchFrame{}, 900);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Recovery),
                        static_cast<int>(discard.screen()));
  uiTap(discard, 233, 338, 1000);
  TEST_ASSERT_TRUE(discard.takeRecoveryDiscardRequest());
  TEST_ASSERT_FALSE(discard.takeRecoveryRestoreRequest());
  discard.showCanvas();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(discard.screen()));

  Ui normal;
  normal.begin(0);
  normal.update(TouchFrame{}, 900);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(normal.screen()));
}

static void test_ui_hsv_picker_flow() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  uiTap(ui, 136, 24, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Palette),
                        static_cast<int>(ui.sheet()));
  uiTap(ui, 274, 280, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Hsv),
                        static_cast<int>(ui.sheet()));

  const uint16_t before = ui.color();
  uiTap(ui, 200, 234, 1400);
  uiTap(ui, 200, 330, 1600);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Hsv),
                        static_cast<int>(ui.sheet()));
  uiTap(ui, 233, 446, 1800);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_TRUE(ui.color() != before);
  TEST_ASSERT_TRUE(ui.color() != kBlack);
  TEST_ASSERT_TRUE(ui.color() != kWhite);

  uiTap(ui, 136, 24, 2000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Palette),
                        static_cast<int>(ui.sheet()));
  uiTap(ui, 274, 280, 2200);
  uiTap(ui, 87, 446, 2400);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_TRUE(ui.color() != before);
}

static void test_ui_hsv_tap_dirty_bounds() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  uiTap(ui, 136, 24, 1000);
  uiTap(ui, 274, 280, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Hsv),
                        static_cast<int>(ui.sheet()));

  ui.clearDirty();
  uiTap(ui, 200, 234, 1400);  // hue bar
  TEST_ASSERT_EQUAL_INT(5, ui.dirtyCount());
  for (int i = 0; i < ui.dirtyCount(); ++i) {
    const Rect r = ui.dirtyRect(i);
    TEST_ASSERT_FALSE(r == ui_layout::kHsvHuePaint);
    TEST_ASSERT_TRUE(r.w < ui_layout::kSheetHsv.w ||
                     r.h < ui_layout::kSheetHsv.h);
    TEST_ASSERT_TRUE(ui_layout::kSheetHsv.contains({r.x, r.y}));
    TEST_ASSERT_TRUE(ui_layout::kSheetHsv.contains(
        {static_cast<int16_t>(r.right() - 1),
         static_cast<int16_t>(r.bottom() - 1)}));
  }

  ui.clearDirty();
  uiTap(ui, 200, 330, 1600);  // saturation/value square
  TEST_ASSERT_EQUAL_INT(4, ui.dirtyCount());
  for (int i = 0; i < ui.dirtyCount(); ++i) {
    const Rect r = ui.dirtyRect(i);
    TEST_ASSERT_FALSE(r == ui_layout::kHsvSquarePaint);
    TEST_ASSERT_TRUE((r.w <= 21 && r.h <= 21) ||
                     r == ui_layout::kHsvPreview ||
                     r == ui_layout::kHsvReadout);
  }
}

static void test_ui_tool_options_and_shape_fill() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  uiTap(ui, 40, 24, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Tools),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitToolOption({60, 450}));
  TEST_ASSERT_EQUAL_INT(1, ui_layout::hitToolOption({150, 450}));
  TEST_ASSERT_EQUAL_INT(2, ui_layout::hitToolOption({240, 450}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitToolOption({100, 410}));

  // Select Rect (index 3), then toggle filled/outline from the options row.
  uiTap(ui, 64, 362, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::None),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Tool::Rect),
                        static_cast<int>(ui.tool()));
  TEST_ASSERT_FALSE(ui.shapeFilled());

  uiTap(ui, 40, 24, 1400);
  uiTap(ui, 260, 454, 1600);
  TEST_ASSERT_TRUE(ui.shapeFilled());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Tools),
                        static_cast<int>(ui.sheet()));
  uiTap(ui, 164, 454, 1800);
  TEST_ASSERT_FALSE(ui.shapeFilled());

  // The size entry point opens the existing size sheet.
  uiTap(ui, 64, 454, 2000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Size),
                        static_cast<int>(ui.sheet()));

  // Outline/filled is meaningless for the brush; tapping it must not change
  // the mode or close the sheet.
  uiTap(ui, 40, 24, 2200);
  uiTap(ui, 86, 310, 2400);  // Brush
  uiTap(ui, 40, 24, 2600);
  uiTap(ui, 260, 454, 2800);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Sheet::Tools),
                        static_cast<int>(ui.sheet()));
  TEST_ASSERT_FALSE(ui.shapeFilled());
}

static void test_ui_undo_requests_and_availability() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  uiTap(ui, 184, 24, 1000);
  TEST_ASSERT_FALSE(ui.takeUndoRequest());
  TEST_ASSERT_FALSE(ui.takeRedoRequest());

  ui.setUndoRedoAvailable(true, true);
  uiTap(ui, 184, 24, 1200);
  TEST_ASSERT_TRUE(ui.takeUndoRequest());
  TEST_ASSERT_FALSE(ui.takeUndoRequest());

  uiTap(ui, 232, 24, 1400);
  TEST_ASSERT_TRUE(ui.takeRedoRequest());
  TEST_ASSERT_FALSE(ui.takeRedoRequest());
}

static Calibration panelCalibration() {
  const CalibrationPoint points[4] = {
      {301, 3795, 0, 0},      // top-left
      {260, 298, 319, 0},     // top-right
      {3862, 3787, 0, 479},   // bottom-left
      {3845, 301, 319, 479},  // bottom-right
  };
  return makeFourPointCalibration(points);
}

static void test_touch_calibration_corners() {
  const Calibration cal = panelCalibration();

  Point p = applyCalibration(cal, 301, 3795);
  TEST_ASSERT_INT16_WITHIN(1, 0, p.x);
  TEST_ASSERT_INT16_WITHIN(1, 0, p.y);

  p = applyCalibration(cal, 260, 298);
  TEST_ASSERT_INT16_WITHIN(1, 319, p.x);
  TEST_ASSERT_INT16_WITHIN(1, 0, p.y);

  p = applyCalibration(cal, 3862, 3787);
  TEST_ASSERT_INT16_WITHIN(1, 0, p.x);
  TEST_ASSERT_INT16_WITHIN(1, 479, p.y);

  p = applyCalibration(cal, 3845, 301);
  TEST_ASSERT_INT16_WITHIN(1, 319, p.x);
  TEST_ASSERT_INT16_WITHIN(1, 479, p.y);
}

static void test_touch_calibration_center_and_clamp() {
  const Calibration cal = panelCalibration();

  Point p = applyCalibration(cal, 2067, 2045);
  TEST_ASSERT_INT16_WITHIN(2, 159, p.x);
  TEST_ASSERT_INT16_WITHIN(2, 239, p.y);

  p = applyCalibration(cal, 0, 0);
  TEST_ASSERT_EQUAL_INT(319, p.x);
  TEST_ASSERT_EQUAL_INT(0, p.y);

  p = applyCalibration(cal, 4095, 4095);
  TEST_ASSERT_EQUAL_INT(0, p.x);
  TEST_ASSERT_EQUAL_INT(479, p.y);
}

static void test_touch_calibration_plausibility() {
  const CalibrationPoint points[4] = {
      {301, 3795, 0, 0},      // top-left
      {260, 298, 319, 0},     // top-right
      {3862, 3787, 0, 479},   // bottom-left
      {3845, 301, 319, 479},  // bottom-right
  };
  const Calibration cal = makeFourPointCalibration(points);
  TEST_ASSERT_TRUE(isCalibrationPlausible(cal));
  TEST_ASSERT_TRUE(calibrationMatchesPoints(cal, points));

  CalibrationPoint wrong[4] = {
      {301, 3795, 0, 0},
      {260, 298, 200, 0},
      {3862, 3787, 0, 479},
      {3845, 301, 319, 479},
  };
  TEST_ASSERT_FALSE(calibrationMatchesPoints(cal, wrong, 5));

  Calibration zero;
  TEST_ASSERT_FALSE(isCalibrationPlausible(zero));

  Calibration absurd = panelCalibration();
  absurd.d = 200000;
  TEST_ASSERT_FALSE(isCalibrationPlausible(absurd));
}

static void test_settings_codec_round_trip() {
  Settings source;
  source.rotation = 2;
  source.brightness = 173;
  source.otaEnabled = false;
  source.flags = 0xA5;
  source.calibration = panelCalibration();
  std::strcpy(source.lastOpened, "img0012.pnt");

  uint8_t blob[settings_codec::kBlobSize] = {};
  settings_codec::encode(source, blob);

  Settings decoded;
  TEST_ASSERT_TRUE(settings_codec::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_EQUAL_INT(source.rotation, decoded.rotation);
  TEST_ASSERT_EQUAL_INT(source.brightness, decoded.brightness);
  TEST_ASSERT_EQUAL_INT(source.otaEnabled, decoded.otaEnabled);
  TEST_ASSERT_EQUAL_INT(source.flags, decoded.flags);
  TEST_ASSERT_EQUAL_STRING(source.lastOpened, decoded.lastOpened);
  TEST_ASSERT_EQUAL_INT(source.calibration.a, decoded.calibration.a);
  TEST_ASSERT_EQUAL_INT(source.calibration.b, decoded.calibration.b);
  TEST_ASSERT_EQUAL_INT(source.calibration.c, decoded.calibration.c);
  TEST_ASSERT_EQUAL_INT(source.calibration.d, decoded.calibration.d);
  TEST_ASSERT_EQUAL_INT(source.calibration.e, decoded.calibration.e);
  TEST_ASSERT_EQUAL_INT(source.calibration.f, decoded.calibration.f);

  blob[8] ^= 0x01;
  TEST_ASSERT_FALSE(settings_codec::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_FALSE(settings_codec::decode(blob, sizeof(blob) - 1, decoded));
}

static void test_settings_codec_v1_migration() {
  uint8_t blob[settings_codec::kHeaderSize + settings_codec::kPayloadSize1] = {};
  uint8_t* payload = blob + settings_codec::kHeaderSize;
  payload[0] = 2;
  payload[1] = 200;
  payload[2] = 1;
  payload[3] = 0;
  const uint32_t coefficients[6] = {0x00010000u, 0u, 10u, 0u,
                                    0x0000F000u, 20u};
  for (int i = 0; i < 6; ++i) {
    settings_codec::writeU32(payload + 4 + i * 4, coefficients[i]);
  }
  settings_codec::writeU32(blob, settings_codec::kMagic);
  settings_codec::writeU16(blob + 4, settings_codec::kVersion1);
  settings_codec::writeU16(blob + 6,
                           static_cast<uint16_t>(settings_codec::kPayloadSize1));
  settings_codec::writeU32(
      blob + 8, settings_codec::checksum(payload, settings_codec::kPayloadSize1));

  Settings decoded;
  TEST_ASSERT_TRUE(settings_codec::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_EQUAL_INT(2, decoded.rotation);
  TEST_ASSERT_EQUAL_INT(200, decoded.brightness);
  TEST_ASSERT_TRUE(decoded.otaEnabled);
  TEST_ASSERT_EQUAL_INT(0, decoded.flags);
  TEST_ASSERT_EQUAL_STRING("", decoded.lastOpened);
  TEST_ASSERT_EQUAL_HEX32(0x00010000u,
                          static_cast<uint32_t>(decoded.calibration.a));
  TEST_ASSERT_EQUAL_HEX32(0x0000F000u,
                          static_cast<uint32_t>(decoded.calibration.e));

  // A v1 payload with a trailing byte is not a valid blob.
  TEST_ASSERT_FALSE(
      settings_codec::decode(blob, sizeof(blob) - 1, decoded));
}

static void test_settings_codec_rejects_invalid_last_opened() {
  Settings source;
  source.calibration = panelCalibration();
  std::strcpy(source.lastOpened, "bad/name.pnt");

  uint8_t blob[settings_codec::kBlobSize] = {};
  settings_codec::encode(source, blob);
  Settings decoded;
  TEST_ASSERT_TRUE(settings_codec::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_EQUAL_STRING("", decoded.lastOpened);

  std::strcpy(source.lastOpened, kAutosaveFileName);
  settings_codec::encode(source, blob);
  TEST_ASSERT_TRUE(settings_codec::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_EQUAL_STRING("", decoded.lastOpened);
}

static TouchPoint touchSample(int16_t x, int16_t y, bool pressed) {
  return TouchPoint{x, y, pressed};
}

static void test_touch_state_debounce_and_tap() {
  TouchStateMachine sm;

  TouchFrame f = sm.update(touchSample(10, 10, true), 0);
  TEST_ASSERT_FALSE(f.down);

  f = sm.update(touchSample(10, 10, true), 10);
  TEST_ASSERT_TRUE(f.down);
  TEST_ASSERT_TRUE(f.pressed);
  TEST_ASSERT_EQUAL_INT(10, f.x);
  TEST_ASSERT_EQUAL_INT(10, f.y);

  f = sm.update(touchSample(0, 0, false), 20);
  TEST_ASSERT_FALSE(f.up);
  TEST_ASSERT_TRUE(f.pressed);

  f = sm.update(touchSample(0, 0, false), 30);
  TEST_ASSERT_TRUE(f.up);
  TEST_ASSERT_TRUE(f.tap);
  TEST_ASSERT_FALSE(f.pressed);
  TEST_ASSERT_EQUAL_INT(10, f.x);
  TEST_ASSERT_EQUAL_INT(10, f.y);
  TEST_ASSERT_EQUAL_UINT32(20, f.durationMs);

  f = sm.update(touchSample(0, 0, false), 40);
  TEST_ASSERT_FALSE(f.up);
  TEST_ASSERT_FALSE(f.tap);

  f = sm.update(touchSample(20, 20, true), 50);
  TEST_ASSERT_FALSE(f.down);
  f = sm.update(touchSample(20, 20, true), 60);
  TEST_ASSERT_TRUE(f.down);
  f = sm.update(touchSample(0, 0, false), 70);
  TEST_ASSERT_FALSE(f.up);
  f = sm.update(touchSample(0, 0, false), 80);
  TEST_ASSERT_TRUE(f.up);
  TEST_ASSERT_TRUE(f.tap);
}

static void test_touch_state_move_threshold() {
  TouchStateMachine sm;

  sm.update(touchSample(10, 10, true), 0);
  TouchFrame f = sm.update(touchSample(10, 10, true), 10);
  TEST_ASSERT_TRUE(f.down);

  f = sm.update(touchSample(11, 10, true), 20);
  TEST_ASSERT_FALSE(f.move);

  f = sm.update(touchSample(12, 10, true), 30);
  TEST_ASSERT_TRUE(f.move);
  TEST_ASSERT_EQUAL_INT(12, f.x);

  f = sm.update(touchSample(30, 10, true), 40);
  TEST_ASSERT_TRUE(f.move);

  f = sm.update(touchSample(0, 0, false), 50);
  TEST_ASSERT_FALSE(f.up);
  f = sm.update(touchSample(0, 0, false), 60);
  TEST_ASSERT_TRUE(f.up);
  TEST_ASSERT_FALSE(f.tap);
}

static void test_touch_state_long_press() {
  TouchStateMachine sm;

  sm.update(touchSample(50, 50, true), 0);
  TouchFrame f = sm.update(touchSample(50, 50, true), 10);
  TEST_ASSERT_TRUE(f.down);

  f = sm.update(touchSample(50, 50, true), 609);
  TEST_ASSERT_FALSE(f.longPress);

  f = sm.update(touchSample(50, 50, true), 610);
  TEST_ASSERT_TRUE(f.longPress);

  f = sm.update(touchSample(50, 50, true), 620);
  TEST_ASSERT_FALSE(f.longPress);

  f = sm.update(touchSample(0, 0, false), 630);
  TEST_ASSERT_FALSE(f.up);
  f = sm.update(touchSample(0, 0, false), 640);
  TEST_ASSERT_TRUE(f.up);
  TEST_ASSERT_FALSE(f.tap);
  TEST_ASSERT_EQUAL_UINT32(630, f.durationMs);
}

static void test_touch_state_drag_cancels_long_press() {
  TouchStateMachine sm;

  sm.update(touchSample(50, 50, true), 0);
  sm.update(touchSample(50, 50, true), 10);

  TouchFrame f = sm.update(touchSample(80, 50, true), 20);
  TEST_ASSERT_TRUE(f.move);

  f = sm.update(touchSample(80, 50, true), 700);
  TEST_ASSERT_FALSE(f.longPress);

  f = sm.update(touchSample(0, 0, false), 710);
  TEST_ASSERT_FALSE(f.up);
  f = sm.update(touchSample(0, 0, false), 720);
  TEST_ASSERT_TRUE(f.up);
  TEST_ASSERT_FALSE(f.tap);
}

static void test_touch_state_release_debounce() {
  TouchStateMachine sm;

  sm.update(touchSample(20, 20, true), 0);
  sm.update(touchSample(20, 20, true), 10);

  TouchFrame f = sm.update(touchSample(0, 0, false), 20);
  TEST_ASSERT_FALSE(f.up);
  TEST_ASSERT_TRUE(f.pressed);

  f = sm.update(touchSample(20, 20, true), 30);
  TEST_ASSERT_FALSE(f.up);
  TEST_ASSERT_TRUE(f.pressed);

  f = sm.update(touchSample(0, 0, false), 40);
  TEST_ASSERT_FALSE(f.up);
  TEST_ASSERT_TRUE(f.pressed);

  f = sm.update(touchSample(0, 0, false), 50);
  TEST_ASSERT_TRUE(f.up);
  TEST_ASSERT_TRUE(f.tap);
}

static void test_font_covers_full_table() {
  TEST_ASSERT_EQUAL_INT(0, font::kFirstChar);
  TEST_ASSERT_EQUAL_INT(255, font::kLastChar);
  TEST_ASSERT_EQUAL_INT(5, font::kGlyphWidth);
  TEST_ASSERT_EQUAL_INT(8, font::kGlyphHeight);
}

static void test_ui_layout_top_bar() {
  for (int i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_INT(44, ui_layout::kButtons[i].w);
    TEST_ASSERT_EQUAL_INT(44, ui_layout::kButtons[i].h);
    TEST_ASSERT_EQUAL_INT(2, ui_layout::kButtons[i].y);
    TEST_ASSERT_TRUE(ui_layout::kButtons[i].bottom() <= config::kTopBarHeight);
    TEST_ASSERT_TRUE(ui_layout::kButtons[i].right() <= config::kScreenWidth);
    if (i > 0) {
      TEST_ASSERT_EQUAL_INT(4, ui_layout::kButtons[i].x - ui_layout::kButtons[i - 1].right());
    }
  }

  TEST_ASSERT_EQUAL_INT(18, ui_layout::kButtons[0].x);
  TEST_ASSERT_EQUAL_INT(258, ui_layout::kButtons[5].x);
  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitTopBarButton({40, 24}));
  TEST_ASSERT_EQUAL_INT(5, ui_layout::hitTopBarButton({280, 24}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitTopBarButton({17, 24}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitTopBarButton({63, 24}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitTopBarButton({20, 46}));
  TEST_ASSERT_TRUE(ui_layout::buttonRect(-1).empty());
  TEST_ASSERT_TRUE(ui_layout::buttonRect(6).empty());
}

static void test_ui_layout_canvas_and_sheets() {
  TEST_ASSERT_EQUAL_INT(0, ui_layout::kCanvas.x);
  TEST_ASSERT_EQUAL_INT(config::kTopBarHeight, ui_layout::kCanvas.y);
  TEST_ASSERT_EQUAL_INT(config::kScreenWidth, ui_layout::kCanvas.w);
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight - config::kTopBarHeight,
                        ui_layout::kCanvas.h);
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight, ui_layout::kCanvas.bottom());

  Point clamped = ui_layout::clampCanvasPoint({-5, 10});
  TEST_ASSERT_EQUAL_INT(0, clamped.x);
  TEST_ASSERT_EQUAL_INT(config::kTopBarHeight, clamped.y);
  clamped = ui_layout::clampCanvasPoint({999, 999});
  TEST_ASSERT_EQUAL_INT(config::kScreenWidth - 1, clamped.x);
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight - 1, clamped.y);
  clamped = ui_layout::clampCanvasPoint({160, 240});
  TEST_ASSERT_EQUAL_INT(160, clamped.x);
  TEST_ASSERT_EQUAL_INT(240, clamped.y);

  TEST_ASSERT_TRUE(ui_layout::sheetRect(Sheet::None).empty());
  TEST_ASSERT_EQUAL_INT(260, ui_layout::kSheetTools.y);
  TEST_ASSERT_EQUAL_INT(220, ui_layout::kSheetTools.h);
  TEST_ASSERT_EQUAL_INT(260, ui_layout::kSheetPalette.y);
  TEST_ASSERT_EQUAL_INT(220, ui_layout::kSheetPalette.h);
  TEST_ASSERT_EQUAL_INT(360, ui_layout::kSheetSize.y);
  TEST_ASSERT_EQUAL_INT(120, ui_layout::kSheetSize.h);
  TEST_ASSERT_EQUAL_INT(140, ui_layout::kSheetMenu.y);
  TEST_ASSERT_EQUAL_INT(340, ui_layout::kSheetMenu.h);
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight, ui_layout::kSheetTools.bottom());
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight, ui_layout::kSheetPalette.bottom());
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight, ui_layout::kSheetSize.bottom());
  TEST_ASSERT_EQUAL_INT(config::kScreenHeight, ui_layout::kSheetMenu.bottom());
  TEST_ASSERT_EQUAL_INT(16, config::kScreenHeight - ui_layout::kToast.bottom());

  TEST_ASSERT_TRUE(ui_layout::hitBackButton({40, 24}));
  TEST_ASSERT_FALSE(ui_layout::hitBackButton({70, 24}));
}

static void test_ui_layout_sheet_buttons() {
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::sheetButton(Sheet::None));
  TEST_ASSERT_EQUAL_INT(0, ui_layout::sheetButton(Sheet::Tools));
  TEST_ASSERT_EQUAL_INT(1, ui_layout::sheetButton(Sheet::Size));
  TEST_ASSERT_EQUAL_INT(2, ui_layout::sheetButton(Sheet::Palette));
  TEST_ASSERT_EQUAL_INT(5, ui_layout::sheetButton(Sheet::Menu));
}

static void test_ui_layout_calibration() {
  TEST_ASSERT_TRUE(ui_layout::kCalibrateButton.bottom() <= config::kScreenHeight);
  TEST_ASSERT_TRUE(ui_layout::kCalibrationCancel.bottom() <= config::kScreenHeight);
  TEST_ASSERT_TRUE(ui_layout::kCalibrateButton.contains(
      {20, static_cast<int16_t>(ui_layout::kCalibrateButton.y + 1)}));
  TEST_ASSERT_TRUE(ui_layout::kCalibrationCancel.contains({120, 440}));
  TEST_ASSERT_TRUE(ui_layout::kStorageGaugeArea.contains(
      {ui_layout::kStorageBar.x, ui_layout::kStorageBar.y}));
  TEST_ASSERT_TRUE(ui_layout::kStorageGaugeArea.contains(
      {ui_layout::kStorageBar.right() - 1, ui_layout::kStorageBar.bottom() - 1}));
  TEST_ASSERT_TRUE(ui_layout::kStorageBar.y >
                   ui_layout::kStorageDetailsButton.bottom());
  TEST_ASSERT_TRUE(ui_layout::kStorageDetailsButton.bottom() <=
                   config::kScreenHeight);
  TEST_ASSERT_EQUAL_INT(ui_layout::kCalibrateButton.y,
                        ui_layout::kStorageDetailsButton.y);
  TEST_ASSERT_TRUE(ui_layout::kCalibrateButton.h >= 44);
  TEST_ASSERT_TRUE(ui_layout::kStorageDetailsButton.h >= 44);

  TEST_ASSERT_EQUAL_INT(28, ui_layout::calibrationTarget(0).x);
  TEST_ASSERT_EQUAL_INT(96, ui_layout::calibrationTarget(0).y);
  TEST_ASSERT_EQUAL_INT(292, ui_layout::calibrationTarget(1).x);
  TEST_ASSERT_EQUAL_INT(408, ui_layout::calibrationTarget(2).y);
  TEST_ASSERT_EQUAL_INT(292, ui_layout::calibrationTarget(3).x);
  TEST_ASSERT_EQUAL_INT(0, ui_layout::calibrationTarget(-1).x);
  TEST_ASSERT_EQUAL_INT(0, ui_layout::calibrationTarget(4).x);
}

static void test_ui_layout_sheet_hits() {
  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Tools, {86, 310}));
  TEST_ASSERT_EQUAL_INT(6, ui_layout::hitSheetItem(Sheet::Tools, {86, 410}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitSheetItem(Sheet::Tools, {86, 454}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitSheetItem(Sheet::Tools, {86, 480}));
  const Rect optionRects[3] = {ui_layout::kToolsSizeButton,
                               ui_layout::kToolsOutlineButton,
                               ui_layout::kToolsFilledButton};
  for (const Rect& r : optionRects) {
    TEST_ASSERT_TRUE(ui_layout::kSheetTools.contains({r.x, r.y}));
    TEST_ASSERT_TRUE(ui_layout::kSheetTools.contains(
        {static_cast<int16_t>(r.right() - 1),
         static_cast<int16_t>(r.bottom() - 1)}));
    TEST_ASSERT_TRUE(ui_layout::kToolsOptionsRow.contains({r.x, r.y}));
  }

  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Palette, {50, 318}));
  TEST_ASSERT_EQUAL_INT(23, ui_layout::hitSheetItem(Sheet::Palette, {270, 450}));
  TEST_ASSERT_TRUE(ui_layout::hitPaletteHsv({274, 280}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitSheetItem(Sheet::Palette, {274, 280}));

  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Size, {40, 428}));
  TEST_ASSERT_EQUAL_INT(6, ui_layout::hitSheetItem(Sheet::Size, {280, 428}));

  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Menu, {160, 198}));
  TEST_ASSERT_EQUAL_INT(6, ui_layout::hitSheetItem(Sheet::Menu, {160, 438}));
  TEST_ASSERT_EQUAL_INT(6, ui_layout::hitSheetItem(Sheet::Menu, {160, 470}));
  TEST_ASSERT_TRUE(ui_layout::menuItemRect(6).bottom() <=
                   ui_layout::kSheetMenu.bottom());
}

namespace {

constexpr size_t kStorageCanvasWidth = 320;
constexpr size_t kStorageCanvasHeight = 432;
constexpr size_t kStorageCanvasPixels = kStorageCanvasWidth * kStorageCanvasHeight;
// Worst case is roughly two bytes per pixel plus a token per row, plus the
// thumbnail and header.
constexpr size_t kMaxPntBytes = 300000;

uint16_t gPntSource[kStorageCanvasPixels];
uint16_t gPntDecoded[kStorageCanvasPixels];
uint8_t gPntFile[kMaxPntBytes];

uint16_t pntPattern(size_t index) {
  if ((index / 31) % 4 == 0) {
    return 0x1234;
  }
  if ((index / 7) % 5 == 0) {
    return 0xABCD;
  }
  const uint32_t mixed = static_cast<uint32_t>(index) * 2654435761u + 0x9E3779B9u;
  return static_cast<uint16_t>(mixed >> 16);
}

void fillPntPattern(uint16_t* pixels, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    pixels[i] = pntPattern(i);
  }
}

}  // namespace

static void test_crc32_known_vectors() {
  const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0x00000000u, crc32(nullptr, 0));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32(text, sizeof(text)));

  const uint8_t zeroes[4] = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX32(0x2144DF1Cu, crc32(zeroes, sizeof(zeroes)));
}

static void test_crc32_incremental_matches_oneshot() {
  const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  uint32_t state = 0;
  state = crc32Update(state, text, 4);
  state = crc32Update(state, text + 4, 5);
  TEST_ASSERT_EQUAL_HEX32(crc32(text, sizeof(text)), state);
}

static void test_rle_round_trip_runs_and_literals() {
  const uint16_t source[] = {0x1111, 0x1111, 0x1111, 0x2222, 0x3333, 0x4444,
                             0x4444, 0x5555, 0x6666, 0x7777, 0x7777, 0x7777};
  constexpr size_t count = sizeof(source) / sizeof(source[0]);
  uint8_t encoded[64] = {};
  uint16_t decoded[count] = {};

  const size_t length = rleEncode(source, count, encoded, sizeof(encoded));
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(length));
  TEST_ASSERT_TRUE(rleValidate(encoded, length, count));
  TEST_ASSERT_TRUE(rleDecode(encoded, length, decoded, count));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(source),
                                reinterpret_cast<const uint8_t*>(decoded),
                                sizeof(source));
}

static void test_rle_encodes_worst_case_within_bound() {
  uint16_t source[1000];
  for (size_t i = 0; i < 1000; ++i) {
    source[i] = static_cast<uint16_t>(i & 1 ? 0xAAAA : 0x5555);
  }
  uint8_t encoded[2 + 1000 * 2] = {};
  uint16_t decoded[1000] = {};

  const size_t length = rleEncode(source, 1000, encoded, sizeof(encoded));
  TEST_ASSERT_EQUAL_UINT32(2 + 1000 * 2, static_cast<uint32_t>(length));
  TEST_ASSERT_TRUE(rleDecode(encoded, length, decoded, sizeof(decoded) / sizeof(decoded[0])));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(source),
                                reinterpret_cast<const uint8_t*>(decoded), sizeof(source));
}

static void test_rle_rejects_capacity_and_corrupt_streams() {
  const uint16_t run[] = {0x1111, 0x1111, 0x1111};
  const uint16_t literal[] = {0x1111, 0x2222};
  uint8_t encoded[16] = {};

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(rleEncode(run, 3, encoded, 3)));
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(rleEncode(run, 3, encoded, sizeof(encoded))));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(rleEncode(literal, 2, encoded, 5)));

  uint16_t out[4] = {};

  // Zero-length run, then a value.
  const uint8_t zeroRun[] = {0x00, 0x80, 0x11, 0x11};
  TEST_ASSERT_FALSE(rleDecode(zeroRun, sizeof(zeroRun), out, 4));

  // Zero-length literal.
  const uint8_t zeroLiteral[] = {0x00, 0x00};
  TEST_ASSERT_FALSE(rleDecode(zeroLiteral, sizeof(zeroLiteral), out, 4));

  // Run longer than the remaining pixels.
  const uint8_t longRun[] = {0x05, 0x80, 0x11, 0x11};
  TEST_ASSERT_FALSE(rleDecode(longRun, sizeof(longRun), out, 4));

  // Truncated run value.
  const uint8_t shortRun[] = {0x02, 0x80, 0x11};
  TEST_ASSERT_FALSE(rleDecode(shortRun, sizeof(shortRun), out, 4));

  // Truncated literal payload.
  const uint8_t shortLiteral[] = {0x02, 0x00, 0x11, 0x11};
  TEST_ASSERT_FALSE(rleDecode(shortLiteral, sizeof(shortLiteral), out, 4));

  // Valid stream plus a trailing byte.
  const uint8_t trailing[] = {0x01, 0x00, 0x11, 0x11, 0x00};
  TEST_ASSERT_FALSE(rleDecode(trailing, sizeof(trailing), out, 1));

  // No pixels requested but bytes present.
  TEST_ASSERT_FALSE(rleDecode(trailing, 1, out, 0));
}

static void test_pnt_header_round_trip_and_validation() {
  pnt::Header header;
  header.width = 320;
  header.height = 432;
  header.rleLength = 2048;
  header.thumbnailOffset = static_cast<uint32_t>(pnt::kHeaderSize + header.rleLength);
  header.crc = 0xDEADBEEFu;

  uint8_t bytes[pnt::kHeaderSize] = {};
  pnt::encodeHeader(bytes, header);

  pnt::Header decoded;
  TEST_ASSERT_TRUE(pnt::decodeHeader(bytes, sizeof(bytes), decoded));
  TEST_ASSERT_EQUAL_UINT16(header.version, decoded.version);
  TEST_ASSERT_EQUAL_UINT16(header.width, decoded.width);
  TEST_ASSERT_EQUAL_UINT16(header.height, decoded.height);
  TEST_ASSERT_EQUAL_UINT32(header.rleLength, decoded.rleLength);
  TEST_ASSERT_EQUAL_UINT32(header.thumbnailOffset, decoded.thumbnailOffset);
  TEST_ASSERT_EQUAL_UINT32(header.crc, decoded.crc);

  uint8_t corrupt[pnt::kHeaderSize];

  std::memcpy(corrupt, bytes, sizeof(bytes));
  corrupt[0] ^= 0x01;
  TEST_ASSERT_FALSE(pnt::decodeHeader(corrupt, sizeof(corrupt), decoded));

  std::memcpy(corrupt, bytes, sizeof(bytes));
  corrupt[4] = 2;
  TEST_ASSERT_FALSE(pnt::decodeHeader(corrupt, sizeof(corrupt), decoded));

  std::memcpy(corrupt, bytes, sizeof(bytes));
  corrupt[6] = 1;
  TEST_ASSERT_FALSE(pnt::decodeHeader(corrupt, sizeof(corrupt), decoded));

  std::memcpy(corrupt, bytes, sizeof(bytes));
  corrupt[24] = 1;
  TEST_ASSERT_FALSE(pnt::decodeHeader(corrupt, sizeof(corrupt), decoded));

  std::memcpy(corrupt, bytes, sizeof(bytes));
  corrupt[16] ^= 0x01;
  TEST_ASSERT_FALSE(pnt::decodeHeader(corrupt, sizeof(corrupt), decoded));

  std::memcpy(corrupt, bytes, sizeof(bytes));
  corrupt[12] = 0;
  corrupt[13] = 0;
  corrupt[14] = 0;
  corrupt[15] = 0;
  TEST_ASSERT_FALSE(pnt::decodeHeader(corrupt, sizeof(corrupt), decoded));

  TEST_ASSERT_FALSE(pnt::decodeHeader(bytes, pnt::kHeaderSize - 1, decoded));
}

static void test_pnt_round_trip_is_pixel_identical() {
  fillPntPattern(gPntSource, kStorageCanvasPixels);

  const size_t length = pnt::encodeToBuffer(gPntSource, kStorageCanvasWidth, kStorageCanvasWidth,
                                            kStorageCanvasHeight, gPntFile, sizeof(gPntFile));
  TEST_ASSERT_GREATER_THAN_UINT32(static_cast<uint32_t>(pnt::kHeaderSize),
                                  static_cast<uint32_t>(length));

  pnt::Header header;
  TEST_ASSERT_TRUE(pnt::decodeHeader(gPntFile, length, header));
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(kStorageCanvasWidth), header.width);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(kStorageCanvasHeight), header.height);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(pnt::kHeaderSize + header.rleLength),
                           header.thumbnailOffset);
  TEST_ASSERT_EQUAL_UINT32(
      static_cast<uint32_t>(header.thumbnailOffset + pnt::kThumbnailBytes),
      static_cast<uint32_t>(length));

  DocumentInfo info;
  std::memset(gPntDecoded, 0, sizeof(gPntDecoded));
  TEST_ASSERT_TRUE(pnt::decode(gPntFile, length, gPntDecoded, info));
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(kStorageCanvasWidth), info.width);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(kStorageCanvasHeight), info.height);
  TEST_ASSERT_EQUAL_UINT32(header.crc, info.crc);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(gPntSource),
                                reinterpret_cast<const uint8_t*>(gPntDecoded),
                                sizeof(gPntSource));
}

static void test_pnt_worst_case_fits_buffer() {
  for (size_t y = 0; y < kStorageCanvasHeight; ++y) {
    for (size_t x = 0; x < kStorageCanvasWidth; ++x) {
      gPntSource[y * kStorageCanvasWidth + x] =
          static_cast<uint16_t>((x & 1) ? 0xAAAA : 0x5555);
    }
  }

  const size_t length = pnt::encodeToBuffer(gPntSource, kStorageCanvasWidth, kStorageCanvasWidth,
                                            kStorageCanvasHeight, gPntFile, sizeof(gPntFile));
  const size_t worstCase =
      pnt::kHeaderSize + (2 + 2 * kStorageCanvasWidth) * kStorageCanvasHeight +
      pnt::kThumbnailBytes;
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(length));
  TEST_ASSERT_TRUE(length <= worstCase);

  DocumentInfo info;
  std::memset(gPntDecoded, 0, sizeof(gPntDecoded));
  TEST_ASSERT_TRUE(pnt::decode(gPntFile, length, gPntDecoded, info));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(gPntSource),
                                reinterpret_cast<const uint8_t*>(gPntDecoded),
                                sizeof(gPntSource));
}

static void test_pnt_thumbnail_matches_nearest_sample() {
  fillPntPattern(gPntSource, kStorageCanvasPixels);

  const size_t length = pnt::encodeToBuffer(gPntSource, kStorageCanvasWidth, kStorageCanvasWidth,
                                            kStorageCanvasHeight, gPntFile, sizeof(gPntFile));
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(length));

  pnt::Header header;
  TEST_ASSERT_TRUE(pnt::decodeHeader(gPntFile, length, header));

  uint16_t expected[pnt::kThumbnailPixels] = {};
  TEST_ASSERT_TRUE(pnt::buildThumbnail(gPntSource, kStorageCanvasWidth, kStorageCanvasWidth,
                                       kStorageCanvasHeight, expected));

  uint16_t actual[pnt::kThumbnailPixels] = {};
  TEST_ASSERT_TRUE(pnt::decodeThumbnail(gPntFile, length, header, actual));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(expected),
                                reinterpret_cast<const uint8_t*>(actual), sizeof(expected));

  TEST_ASSERT_EQUAL_HEX16(gPntSource[0], actual[0]);
  const size_t lastX = static_cast<size_t>(pnt::kThumbnailWidth - 1) * kStorageCanvasWidth /
                       pnt::kThumbnailWidth;
  const size_t lastY = static_cast<size_t>(pnt::kThumbnailHeight - 1) * kStorageCanvasHeight /
                       pnt::kThumbnailHeight;
  TEST_ASSERT_EQUAL_HEX16(gPntSource[lastY * kStorageCanvasWidth + lastX],
                          actual[pnt::kThumbnailPixels - 1]);
}

static void test_pnt_rejects_corrupt_or_truncated_files() {
  constexpr size_t width = 64;
  constexpr size_t height = 48;

  fillPntPattern(gPntSource, width * height);
  const size_t length =
      pnt::encodeToBuffer(gPntSource, width, width, height, gPntFile, sizeof(gPntFile));
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(length));

  pnt::Header header;
  TEST_ASSERT_TRUE(pnt::decodeHeader(gPntFile, length, header));

  DocumentInfo info;
  uint8_t copy[kMaxPntBytes];

  std::memcpy(copy, gPntFile, length);
  copy[0] ^= 0x01;
  TEST_ASSERT_FALSE(pnt::decode(copy, length, gPntDecoded, info));

  std::memcpy(copy, gPntFile, length);
  copy[4] = 2;
  TEST_ASSERT_FALSE(pnt::decode(copy, length, gPntDecoded, info));

  std::memcpy(copy, gPntFile, length);
  copy[6] = 1;
  TEST_ASSERT_FALSE(pnt::decode(copy, length, gPntDecoded, info));

  std::memcpy(copy, gPntFile, length);
  copy[24] = 1;
  TEST_ASSERT_FALSE(pnt::decode(copy, length, gPntDecoded, info));

  std::memcpy(copy, gPntFile, length);
  copy[pnt::kHeaderSize] ^= 0xFF;
  TEST_ASSERT_FALSE(pnt::decode(copy, length, gPntDecoded, info));

  std::memcpy(copy, gPntFile, length);
  copy[header.thumbnailOffset + 1] ^= 0xFF;
  TEST_ASSERT_FALSE(pnt::decode(copy, length, gPntDecoded, info));

  TEST_ASSERT_FALSE(pnt::decode(gPntFile, length - 1, gPntDecoded, info));
  TEST_ASSERT_FALSE(pnt::decode(gPntFile, length + 1, gPntDecoded, info));
  TEST_ASSERT_FALSE(pnt::decode(gPntFile, pnt::kHeaderSize - 1, gPntDecoded, info));
}

static void test_pnt_decode_does_not_touch_destination_on_bad_crc() {
  constexpr size_t width = 32;
  constexpr size_t height = 24;
  constexpr size_t count = width * height;

  fillPntPattern(gPntSource, count);
  const size_t length =
      pnt::encodeToBuffer(gPntSource, width, width, height, gPntFile, sizeof(gPntFile));
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(length));

  gPntFile[pnt::kHeaderSize] ^= 0xFF;

  for (size_t i = 0; i < count; ++i) {
    gPntDecoded[i] = 0x5A5A;
  }
  DocumentInfo info;
  TEST_ASSERT_FALSE(pnt::decode(gPntFile, length, gPntDecoded, info));
  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL_HEX16(0x5A5A, gPntDecoded[i]);
  }
}

static void test_pnt_rejects_malformed_rle_with_valid_crc() {
  constexpr size_t width = 32;
  constexpr size_t height = 24;
  constexpr size_t count = width * height;

  fillPntPattern(gPntSource, count);
  const size_t length =
      pnt::encodeToBuffer(gPntSource, width, width, height, gPntFile, sizeof(gPntFile));
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(length));

  pnt::Header header;
  TEST_ASSERT_TRUE(pnt::decodeHeader(gPntFile, length, header));

  // Replace the first token with a zero-length run, then repair the CRC so the
  // file passes the checksum but fails RLE validation.
  gPntFile[pnt::kHeaderSize] = 0x00;
  gPntFile[pnt::kHeaderSize + 1] = 0x80;
  header.crc = crc32(gPntFile + pnt::kHeaderSize, length - pnt::kHeaderSize);
  pnt::encodeHeader(gPntFile, header);

  for (size_t i = 0; i < count; ++i) {
    gPntDecoded[i] = 0x5A5A;
  }
  DocumentInfo info;
  TEST_ASSERT_FALSE(pnt::decode(gPntFile, length, gPntDecoded, info));
  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL_HEX16(0x5A5A, gPntDecoded[i]);
  }
}

static void test_pnt_encode_rejects_bad_arguments() {
  fillPntPattern(gPntSource, 16);
  TEST_ASSERT_EQUAL_UINT32(
      0, static_cast<uint32_t>(pnt::encodeToBuffer(nullptr, 4, 4, 4, gPntFile, sizeof(gPntFile))));
  TEST_ASSERT_EQUAL_UINT32(
      0, static_cast<uint32_t>(pnt::encodeToBuffer(gPntSource, 3, 4, 4, gPntFile, sizeof(gPntFile))));
  TEST_ASSERT_EQUAL_UINT32(
      0, static_cast<uint32_t>(pnt::encodeToBuffer(gPntSource, 4, 0, 4, gPntFile, sizeof(gPntFile))));
  TEST_ASSERT_EQUAL_UINT32(
      0, static_cast<uint32_t>(pnt::encodeToBuffer(gPntSource, 4, 4, 0, gPntFile, sizeof(gPntFile))));
  TEST_ASSERT_EQUAL_UINT32(
      0, static_cast<uint32_t>(pnt::encodeToBuffer(gPntSource, 4, 4, 4, gPntFile, 16)));
}

static void test_orientation_and_idle_mapping() {
  const Point p{10, 20};
  const Point flipped = mapCanonicalToDisplay(p, 0);
  TEST_ASSERT_EQUAL_INT16(309, flipped.x);
  TEST_ASSERT_EQUAL_INT16(459, flipped.y);
  TEST_ASSERT_EQUAL_INT16(p.x, mapCanonicalToDisplay(p, 2).x);
  TEST_ASSERT_EQUAL_INT16(p.y, mapCanonicalToDisplay(p, 2).y);
  TEST_ASSERT_EQUAL_INT16(p.x, mapDisplayToCanonical(flipped, 0).x);
  TEST_ASSERT_EQUAL_INT16(p.y, mapDisplayToCanonical(flipped, 0).y);
  TEST_ASSERT_TRUE(isSupportedRotation(0));
  TEST_ASSERT_TRUE(isSupportedRotation(2));
  TEST_ASSERT_FALSE(isSupportedRotation(1));
  TEST_ASSERT_EQUAL_UINT8(2, normalizeRotation(3));

  IdleController idle;
  idle.reset(1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdleLevel::Active),
                        static_cast<int>(idle.update(1000 + config::kIdleDimMs - 1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdleLevel::Dimmed),
                        static_cast<int>(idle.update(1000 + config::kIdleDimMs)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdleLevel::Off),
                        static_cast<int>(idle.update(1000 + config::kIdleOffMs)));
  TEST_ASSERT_TRUE(idle.shouldWakeAndConsume(true));
  idle.noteTouch(1000 + config::kIdleOffMs + 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdleLevel::Active),
                        static_cast<int>(idle.level()));
  TEST_ASSERT_FALSE(idle.shouldWakeAndConsume(true));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(IdleLevel::Active),
                        static_cast<int>(idle.update(1000 + config::kIdleOffMs +
                                                     config::kIdleDimMs - 1)));
}

static void test_boot_health_codec_and_threshold() {
  uint8_t blob[boot_health::kBlobSize] = {};
  boot_health::State state;
  state.badBoots = 2;
  state.valid = true;
  boot_health::encode(state, blob);
  TEST_ASSERT_EQUAL_UINT8('P', blob[0]);
  TEST_ASSERT_EQUAL_UINT8('B', blob[1]);

  boot_health::State decoded;
  TEST_ASSERT_TRUE(boot_health::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_TRUE(decoded.valid);
  TEST_ASSERT_EQUAL_UINT8(2, decoded.badBoots);
  TEST_ASSERT_FALSE(decoded.recoveryRequired());

  state.badBoots = 9;
  boot_health::encode(state, blob);
  TEST_ASSERT_TRUE(boot_health::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_EQUAL_UINT8(config::kBootFailThreshold, decoded.badBoots);
  TEST_ASSERT_TRUE(decoded.recoveryRequired());

  blob[4] ^= 0x01;
  TEST_ASSERT_FALSE(boot_health::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_FALSE(boot_health::decode(blob, sizeof(blob) - 1, decoded));
  TEST_ASSERT_FALSE(boot_health::decode(nullptr, sizeof(blob), decoded));
}

static void test_ui_settings_controls_persist_requests() {
  Ui ui;
  ui.begin(0);
  ui.update(TouchFrame{}, 900);

  uiTap(ui, 280, 24, 1000);
  uiTap(ui, 160, 358, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Settings),
                        static_cast<int>(ui.screen()));
  TEST_ASSERT_TRUE(ui_layout::kRotationButton.h >= 44);
  TEST_ASSERT_TRUE(ui_layout::kSettingsWifiToggle.h >= 44);

  uiTap(ui, 160, ui_layout::kRotationButton.y + 22, 1400);
  TEST_ASSERT_TRUE(ui.takeRotationToggleRequest());
  TEST_ASSERT_FALSE(ui.takeRotationToggleRequest());
  ui.setRotation(0);
  TEST_ASSERT_EQUAL_UINT8(0, ui.rotation());
  uiTap(ui, 160, ui_layout::kRotationButton.y + 22, 1500);
  TEST_ASSERT_TRUE(ui.takeRotationToggleRequest());
  ui.setRotation(2);
  TEST_ASSERT_EQUAL_UINT8(2, ui.rotation());

  ui.setBrightness(192);
  uiTap(ui, ui_layout::kBrightnessDownButton.x + 22,
        ui_layout::kBrightnessDownButton.y + 22, 1600);
  int8_t step = 0;
  TEST_ASSERT_TRUE(ui.takeBrightnessStepRequest(step));
  TEST_ASSERT_EQUAL_INT8(-1, step);
  TEST_ASSERT_FALSE(ui.takeBrightnessStepRequest(step));
  uiTap(ui, ui_layout::kBrightnessUpButton.x + 22,
        ui_layout::kBrightnessUpButton.y + 22, 1700);
  TEST_ASSERT_TRUE(ui.takeBrightnessStepRequest(step));
  TEST_ASSERT_EQUAL_INT8(1, step);

  ui.setBrightness(config::kBrightnessLevels[0]);
  uiTap(ui, ui_layout::kBrightnessDownButton.x + 22,
        ui_layout::kBrightnessDownButton.y + 22, 1750);
  TEST_ASSERT_FALSE(ui.takeBrightnessStepRequest(step));
  ui.setBrightness(
      config::kBrightnessLevels[config::kBrightnessLevelCount - 1]);
  uiTap(ui, ui_layout::kBrightnessUpButton.x + 22,
        ui_layout::kBrightnessUpButton.y + 22, 1760);
  TEST_ASSERT_FALSE(ui.takeBrightnessStepRequest(step));

  NetInfo info;
  info.configured = true;
  info.enabled = true;
  info.state = NetState::Connected;
  ui.setNetworkInfo(info);
  uiTap(ui, 160, ui_layout::kSettingsWifiToggle.y + 22, 1800);
  TEST_ASSERT_TRUE(ui.takeNetworkToggleRequest());
  TEST_ASSERT_FALSE(ui.takeNetworkToggleRequest());

  // Disabling an enabled link must stay possible when OTA storage is low.
  info.lowStorage = true;
  ui.setNetworkInfo(info);
  uiTap(ui, 160, ui_layout::kSettingsWifiToggle.y + 22, 1850);
  TEST_ASSERT_TRUE(ui.takeNetworkToggleRequest());
  TEST_ASSERT_FALSE(ui.takeNetworkToggleRequest());

  uiTap(ui, ui_layout::kAboutButton.x + 20,
        ui_layout::kAboutButton.y + 22, 1900);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::About),
                        static_cast<int>(ui.screen()));
  uiTap(ui, 40, 24, 2000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Settings),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kStorageDetailsButton.x + 20,
        ui_layout::kStorageDetailsButton.y + 22, 2100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Storage),
                        static_cast<int>(ui.screen()));
  uiTap(ui, 40, 24, 2200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Settings),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kSettingsNetworkButton.x + 20,
        ui_layout::kSettingsNetworkButton.y + 22, 2300);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Network),
                        static_cast<int>(ui.screen()));
  uiTap(ui, 40, 24, 2400);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Settings),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kCalibrateButton.x + 20,
        ui_layout::kCalibrateButton.y + 22, 2500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Calibrate),
                        static_cast<int>(ui.screen()));
  uiTap(ui, 160, 454, 2600);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Settings),
                        static_cast<int>(ui.screen()));

  uiTap(ui, 40, 24, 2700);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Canvas),
                        static_cast<int>(ui.screen()));
}

static void test_ui_safe_mode_self_test_flow() {
  Ui ui;
  ui.begin(0);
  ui.setSafeMode(true, true, false, true);
  ui.update(TouchFrame{}, 900);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::SafeMode),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kSafeDisplayTest.x + 20,
        ui_layout::kSafeDisplayTest.y + 22, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::DisplayTest),
                        static_cast<int>(ui.screen()));
  uiTap(ui, ui_layout::kDisplayTestBack.x + 20,
        ui_layout::kDisplayTestBack.y + 22, 1100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::SafeMode),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kSafeTouchTest.x + 20,
        ui_layout::kSafeTouchTest.y + 22, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::TouchTest),
                        static_cast<int>(ui.screen()));
  TouchFrame touch;
  touch.pressed = true;
  touch.down = true;
  touch.x = 120;
  touch.y = 180;
  touch.rawX = 1800;
  touch.rawY = 2600;
  ui.update(touch, 1300);
  uiTap(ui, ui_layout::kTouchTestBack.x + 20,
        ui_layout::kTouchTestBack.y + 22, 1400);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::SafeMode),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kSafeStorageTest.x + 20,
        ui_layout::kSafeStorageTest.y + 22, 1500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Storage),
                        static_cast<int>(ui.screen()));
  uiTap(ui, 40, 24, 1600);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::SafeMode),
                        static_cast<int>(ui.screen()));

  uiTap(ui, ui_layout::kSafeContinue.x + 20,
        ui_layout::kSafeContinue.y + 22, 1700);
  TEST_ASSERT_TRUE(ui.takeSafeModeContinueRequest());
  TEST_ASSERT_FALSE(ui.takeSafeModeContinueRequest());
  uiTap(ui, ui_layout::kSafeReboot.x + 20,
        ui_layout::kSafeReboot.y + 22, 1800);
  TEST_ASSERT_TRUE(ui.takeSafeModeRebootRequest());
  TEST_ASSERT_FALSE(ui.takeSafeModeRebootRequest());
}

static void test_ui_accessibility_primary_targets() {
  const Rect targets[] = {
      ui_layout::kButtons[0],           ui_layout::kButtons[1],
      ui_layout::kButtons[2],           ui_layout::kButtons[3],
      ui_layout::kButtons[4],           ui_layout::kButtons[5],
      ui_layout::kBackButton,           ui_layout::kRotationButton,
      ui_layout::kBrightnessDownButton, ui_layout::kBrightnessUpButton,
      ui_layout::kSettingsWifiToggle,   ui_layout::kCalibrateButton,
      ui_layout::kStorageDetailsButton, ui_layout::kAboutButton,
      ui_layout::kSettingsNetworkButton, ui_layout::kNetworkToggleButton,
      ui_layout::kSafeDisplayTest,      ui_layout::kSafeTouchTest,
      ui_layout::kSafeStorageTest,      ui_layout::kSafeContinue,
      ui_layout::kSafeReboot,           ui_layout::kDisplayTestBack,
      ui_layout::kTouchTestBack,        ui_layout::kCalibrationCancel,
      ui_layout::kConfirmNewCancel,     ui_layout::kConfirmNewOk,
      ui_layout::kGalleryCancel,        ui_layout::kGalleryOpen,
      ui_layout::kGalleryPrev,          ui_layout::kGalleryNext,
      ui_layout::kHsvCancel,            ui_layout::kHsvOk,
      ui_layout::kRecoveryRestore,      ui_layout::kRecoveryDiscard,
  };
  for (const Rect& r : targets) {
    TEST_ASSERT_TRUE(r.w >= 44);
    TEST_ASSERT_TRUE(r.h >= 44);
    TEST_ASSERT_TRUE(ui_layout::kScreen.contains({r.x, r.y}));
    TEST_ASSERT_TRUE(ui_layout::kScreen.contains(
        {static_cast<int16_t>(r.right() - 1),
         static_cast<int16_t>(r.bottom() - 1)}));
  }

  for (int i = 0; i < 7; ++i) {
    TEST_ASSERT_TRUE(ui_layout::toolItemRect(i).h >= 44);
    TEST_ASSERT_TRUE(ui_layout::sizeItemRect(i).w >= 44);
    TEST_ASSERT_TRUE(ui_layout::sizeItemRect(i).h >= 44);
    TEST_ASSERT_TRUE(ui_layout::menuItemRect(i).h >= 44);
  }
  for (int i = 0; i < 24; ++i) {
    TEST_ASSERT_TRUE(ui_layout::paletteSwatchRect(i).w >= 44);
    TEST_ASSERT_TRUE(ui_layout::paletteSwatchRect(i).h >= 44);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rgb565_primaries);
  RUN_TEST(test_expand_boundaries);
  RUN_TEST(test_rgb565_to_rgb888);
  RUN_TEST(test_rgb666_channels);
  RUN_TEST(test_geometry_contains);
  RUN_TEST(test_geometry_intersect);
  RUN_TEST(test_canvas_dimensions);
  RUN_TEST(test_dirty_region_merges_and_clears);
  RUN_TEST(test_dirty_region_keeps_disjoint_rects);
  RUN_TEST(test_canvas_clips_to_bounds_and_tracks_dirty);
  RUN_TEST(test_brush_masks_are_round_and_symmetric);
  RUN_TEST(test_brush_stamp_clips_and_marks_dirty);
  RUN_TEST(test_stroke_is_continuous_and_clipped);
  RUN_TEST(test_stroke_controller_eraser_and_cancel);
  RUN_TEST(test_undo_redo_round_trip);
  RUN_TEST(test_shape_preview_cancel_leaves_no_residue);
  RUN_TEST(test_shape_filled_commit_and_undo);
  RUN_TEST(test_undo_long_action_tiles);
  RUN_TEST(test_shape_commit_rect_and_ellipse_undo);
  RUN_TEST(test_flood_fill_stays_inside_outline);
  RUN_TEST(test_flood_fill_same_colour_is_noop);
  RUN_TEST(test_undo_multiple_actions_and_new_action_discards_redo);
  RUN_TEST(test_undo_compressed_deltas_keep_ten_actions);
  RUN_TEST(test_undo_raw_tile_fallback);
  RUN_TEST(test_undo_handles_canvas_bottom_and_clear);
  RUN_TEST(test_touch_calibration_corners);
  RUN_TEST(test_touch_calibration_center_and_clamp);
  RUN_TEST(test_touch_calibration_plausibility);
  RUN_TEST(test_settings_codec_round_trip);
  RUN_TEST(test_settings_codec_v1_migration);
  RUN_TEST(test_settings_codec_rejects_invalid_last_opened);
  RUN_TEST(test_touch_state_debounce_and_tap);
  RUN_TEST(test_touch_state_move_threshold);
  RUN_TEST(test_touch_state_long_press);
  RUN_TEST(test_touch_state_drag_cancels_long_press);
  RUN_TEST(test_touch_state_release_debounce);
  RUN_TEST(test_font_covers_full_table);
  RUN_TEST(test_ui_layout_top_bar);
  RUN_TEST(test_ui_layout_canvas_and_sheets);
  RUN_TEST(test_ui_layout_sheet_buttons);
  RUN_TEST(test_ui_layout_calibration);
  RUN_TEST(test_ui_layout_sheet_hits);
  RUN_TEST(test_hsv_color_round_trip);
  RUN_TEST(test_ui_layout_new_sheets_and_recent_colors);
  RUN_TEST(test_ui_new_canvas_confirmation_flow);
  RUN_TEST(test_ui_save_and_save_as_flow);
  RUN_TEST(test_ui_gallery_load_delete_and_paging);
  RUN_TEST(test_ui_gallery_empty_and_file_names);
  RUN_TEST(test_ui_storage_details_screen);
  RUN_TEST(test_ui_network_screen_flow);
  RUN_TEST(test_autosave_timer_schedule);
  RUN_TEST(test_storage_space_and_ota_headroom);
  RUN_TEST(test_ui_boot_recovery_prompt);
  RUN_TEST(test_ui_hsv_picker_flow);
  RUN_TEST(test_ui_hsv_tap_dirty_bounds);
  RUN_TEST(test_ui_tool_options_and_shape_fill);
  RUN_TEST(test_ui_undo_requests_and_availability);
  RUN_TEST(test_crc32_known_vectors);
  RUN_TEST(test_crc32_incremental_matches_oneshot);
  RUN_TEST(test_rle_round_trip_runs_and_literals);
  RUN_TEST(test_rle_encodes_worst_case_within_bound);
  RUN_TEST(test_rle_rejects_capacity_and_corrupt_streams);
  RUN_TEST(test_pnt_header_round_trip_and_validation);
  RUN_TEST(test_pnt_round_trip_is_pixel_identical);
  RUN_TEST(test_pnt_worst_case_fits_buffer);
  RUN_TEST(test_pnt_thumbnail_matches_nearest_sample);
  RUN_TEST(test_pnt_rejects_corrupt_or_truncated_files);
  RUN_TEST(test_pnt_decode_does_not_touch_destination_on_bad_crc);
  RUN_TEST(test_pnt_rejects_malformed_rle_with_valid_crc);
  RUN_TEST(test_pnt_encode_rejects_bad_arguments);
  RUN_TEST(test_orientation_and_idle_mapping);
  RUN_TEST(test_boot_health_codec_and_threshold);
  RUN_TEST(test_ui_settings_controls_persist_requests);
  RUN_TEST(test_ui_safe_mode_self_test_flow);
  RUN_TEST(test_ui_accessibility_primary_targets);
  return UNITY_END();
}
