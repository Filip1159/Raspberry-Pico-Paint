#include <unity.h>

#include "config.h"
#include "display/dirty.h"
#include "input/calibration.h"
#include "input/touch_state.h"
#include "paint/color.h"
#include "paint/geometry.h"
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
  RUN_TEST(test_touch_calibration_corners);
  RUN_TEST(test_touch_calibration_center_and_clamp);
  RUN_TEST(test_touch_state_debounce_and_tap);
  RUN_TEST(test_touch_state_move_threshold);
  RUN_TEST(test_touch_state_long_press);
  RUN_TEST(test_touch_state_drag_cancels_long_press);
  RUN_TEST(test_touch_state_release_debounce);
  RUN_TEST(test_font_covers_full_table);
  RUN_TEST(test_ui_layout_top_bar);
  RUN_TEST(test_ui_layout_canvas_and_sheets);
  RUN_TEST(test_ui_layout_sheet_buttons);
  RUN_TEST(test_ui_layout_sheet_hits);
  return UNITY_END();
}
