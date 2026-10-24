#include <unity.h>

#include "config.h"
#include "display/dirty.h"
#include "input/calibration.h"
#include "input/touch_state.h"
#include "paint/brush.h"
#include "paint/canvas.h"
#include "paint/color.h"
#include "paint/geometry.h"
#include "paint/shapes.h"
#include "paint/stroke.h"
#include "paint/undo.h"
#include "storage/settings_codec.h"
#include "ui/font5x7.h"
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
  TEST_ASSERT_EQUAL_INT(4, ui.dirtyCount());
  for (int i = 0; i < ui.dirtyCount(); ++i) {
    const Rect r = ui.dirtyRect(i);
    TEST_ASSERT_TRUE(r.w < ui_layout::kSheetHsv.w ||
                     r.h < ui_layout::kSheetHsv.h);
    TEST_ASSERT_TRUE(ui_layout::kSheetHsv.contains({r.x, r.y}));
    TEST_ASSERT_TRUE(ui_layout::kSheetHsv.contains(
        {static_cast<int16_t>(r.right() - 1),
         static_cast<int16_t>(r.bottom() - 1)}));
  }

  ui.clearDirty();
  uiTap(ui, 200, 330, 1600);  // saturation/value square
  TEST_ASSERT_EQUAL_INT(3, ui.dirtyCount());
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
  source.calibration = panelCalibration();

  uint8_t blob[settings_codec::kBlobSize] = {};
  settings_codec::encode(source, blob);

  Settings decoded;
  TEST_ASSERT_TRUE(settings_codec::decode(blob, sizeof(blob), decoded));
  TEST_ASSERT_EQUAL_INT(source.rotation, decoded.rotation);
  TEST_ASSERT_EQUAL_INT(source.brightness, decoded.brightness);
  TEST_ASSERT_EQUAL_INT(source.otaEnabled, decoded.otaEnabled);
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

  TEST_ASSERT_TRUE(ui_layout::sheetRect(Sheet::None).empty());
  TEST_ASSERT_EQUAL_INT(300, ui_layout::kSheetTools.y);
  TEST_ASSERT_EQUAL_INT(180, ui_layout::kSheetTools.h);
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
  TEST_ASSERT_TRUE(ui_layout::kCalibrateButton.contains({20, 268}));
  TEST_ASSERT_TRUE(ui_layout::kCalibrationCancel.contains({120, 440}));

  TEST_ASSERT_EQUAL_INT(28, ui_layout::calibrationTarget(0).x);
  TEST_ASSERT_EQUAL_INT(96, ui_layout::calibrationTarget(0).y);
  TEST_ASSERT_EQUAL_INT(292, ui_layout::calibrationTarget(1).x);
  TEST_ASSERT_EQUAL_INT(408, ui_layout::calibrationTarget(2).y);
  TEST_ASSERT_EQUAL_INT(292, ui_layout::calibrationTarget(3).x);
  TEST_ASSERT_EQUAL_INT(0, ui_layout::calibrationTarget(-1).x);
  TEST_ASSERT_EQUAL_INT(0, ui_layout::calibrationTarget(4).x);
}

static void test_ui_layout_sheet_hits() {
  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Tools, {86, 353}));
  TEST_ASSERT_EQUAL_INT(6, ui_layout::hitSheetItem(Sheet::Tools, {86, 461}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitSheetItem(Sheet::Tools, {86, 480}));

  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Palette, {50, 318}));
  TEST_ASSERT_EQUAL_INT(23, ui_layout::hitSheetItem(Sheet::Palette, {270, 450}));
  TEST_ASSERT_TRUE(ui_layout::hitPaletteHsv({274, 280}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitSheetItem(Sheet::Palette, {274, 280}));

  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Size, {40, 428}));
  TEST_ASSERT_EQUAL_INT(6, ui_layout::hitSheetItem(Sheet::Size, {280, 428}));

  TEST_ASSERT_EQUAL_INT(0, ui_layout::hitSheetItem(Sheet::Menu, {160, 198}));
  TEST_ASSERT_EQUAL_INT(5, ui_layout::hitSheetItem(Sheet::Menu, {160, 418}));
  TEST_ASSERT_EQUAL_INT(-1, ui_layout::hitSheetItem(Sheet::Menu, {160, 440}));
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
  RUN_TEST(test_ui_hsv_picker_flow);
  RUN_TEST(test_ui_hsv_tap_dirty_bounds);
  RUN_TEST(test_ui_undo_requests_and_availability);
  return UNITY_END();
}
