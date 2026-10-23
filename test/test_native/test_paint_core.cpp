#include <unity.h>

#include "config.h"
#include "display/dirty.h"
#include "paint/color.h"
#include "paint/geometry.h"

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
  return UNITY_END();
}
