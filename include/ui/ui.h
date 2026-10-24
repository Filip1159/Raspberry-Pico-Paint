#pragma once

// Immediate-mode UI shell: layout, hit testing, screens, bottom sheets and
// toasts. The class owns only UI state; rendering is a pure composition into
// the caller's RGB565 framebuffer. Layout constants are Arduino-free so the
// native tests can validate them.

#include <cstdint>

#include "config.h"
#include "display/dirty.h"
#include "input/touch_state.h"
#include "paint/geometry.h"

namespace paint {

enum class Screen : uint8_t {
  Splash,
  Canvas,
  Gallery,
  Settings,
  Calibrate,
  About,
  Recovery,
};

enum class Sheet : uint8_t {
  None,
  Tools,
  Palette,
  Size,
  Menu,
};

enum class Tool : uint8_t {
  Brush,
  Eraser,
  Line,
  Rect,
  Ellipse,
  Fill,
  Picker,
};

namespace ui_layout {

constexpr Rect kScreen{0, 0, config::kScreenWidth, config::kScreenHeight};
constexpr Rect kTopBar{0, 0, config::kScreenWidth, config::kTopBarHeight};
constexpr Rect kCanvas{0, config::kTopBarHeight, config::kCanvasWidth, config::kCanvasHeight};

// Six 44x44 targets: 18 px margins, 4 px gaps, 2 px top/bottom insets.
constexpr Rect kButtons[6] = {
    {18, 2, 44, 44},
    {66, 2, 44, 44},
    {114, 2, 44, 44},
    {162, 2, 44, 44},
    {210, 2, 44, 44},
    {258, 2, 44, 44},
};
constexpr Rect kBackButton{18, 2, 44, 44};

// Settings > Calibrate touch, and the guided four-corner screen.
constexpr Rect kCalibrateButton{20, 268, 280, 44};
constexpr Rect kCalibrationCancel{120, 440, 80, 32};
constexpr Point kCalibrationTargets[4] = {
    {28, 96},   // top-left
    {292, 96},  // top-right
    {28, 408},  // bottom-left
    {292, 408}, // bottom-right
};
constexpr int kCalibrationHitRadius = 56;

constexpr Point calibrationTarget(int index) {
  return (index >= 0 && index < 4) ? kCalibrationTargets[index] : Point{};
}

constexpr Rect kSheetTools{0, 300, 320, 180};
constexpr Rect kSheetPalette{0, 260, 320, 220};
constexpr Rect kSheetSize{0, 360, 320, 120};
constexpr Rect kSheetMenu{0, 140, 320, 340};

constexpr Rect kToast{16, 428, 288, 36};

constexpr Rect sheetRect(Sheet sheet) {
  switch (sheet) {
    case Sheet::Tools:
      return kSheetTools;
    case Sheet::Palette:
      return kSheetPalette;
    case Sheet::Size:
      return kSheetSize;
    case Sheet::Menu:
      return kSheetMenu;
    default:
      return Rect{};
  }
}

constexpr Rect buttonRect(int index) {
  return (index >= 0 && index < 6) ? kButtons[index] : Rect{};
}

constexpr int hitTopBarButton(Point p) {
  for (int i = 0; i < 6; ++i) {
    if (kButtons[i].contains(p)) {
      return i;
    }
  }
  return -1;
}

constexpr bool hitBackButton(Point p) { return kBackButton.contains(p); }

// Tools sheet: two columns, four rows. The seventh cell is empty.
constexpr Rect toolItemRect(int index) {
  if (index < 0 || index >= 7) {
    return Rect{};
  }
  const int col = index % 2;
  const int row = index / 2;
  return Rect{static_cast<int16_t>(16 + col * 148),
              static_cast<int16_t>(338 + row * 36), 140, 30};
}

// Palette sheet: 6x4 swatches plus an HSV shortcut in the title row.
constexpr Rect paletteSwatchRect(int index) {
  if (index < 0 || index >= 24) {
    return Rect{};
  }
  const int col = index % 6;
  const int row = index / 6;
  return Rect{static_cast<int16_t>(32 + col * 44),
              static_cast<int16_t>(300 + row * 44), 36, 36};
}

constexpr Rect kPaletteHsvButton{244, 268, 60, 24};

// Brush-size sheet: seven 36x36 cells.
constexpr Rect sizeItemRect(int index) {
  if (index < 0 || index >= 7) {
    return Rect{};
  }
  return Rect{static_cast<int16_t>(22 + index * 40), 410, 36, 36};
}

// Menu sheet: six full-width rows.
constexpr Rect menuItemRect(int index) {
  if (index < 0 || index >= 6) {
    return Rect{};
  }
  return Rect{16, static_cast<int16_t>(180 + index * 44), 288, 36};
}

constexpr int hitSheetItem(Sheet sheet, Point p) {
  switch (sheet) {
    case Sheet::Tools:
      for (int i = 0; i < 7; ++i) {
        if (toolItemRect(i).contains(p)) {
          return i;
        }
      }
      break;
    case Sheet::Palette:
      for (int i = 0; i < 24; ++i) {
        if (paletteSwatchRect(i).contains(p)) {
          return i;
        }
      }
      break;
    case Sheet::Size:
      for (int i = 0; i < 7; ++i) {
        if (sizeItemRect(i).contains(p)) {
          return i;
        }
      }
      break;
    case Sheet::Menu:
      for (int i = 0; i < 6; ++i) {
        if (menuItemRect(i).contains(p)) {
          return i;
        }
      }
      break;
    default:
      break;
  }
  return -1;
}

constexpr bool hitPaletteHsv(Point p) { return kPaletteHsvButton.contains(p); }

// The top-bar button whose active state follows a sheet.
constexpr int sheetButton(Sheet sheet) {
  switch (sheet) {
    case Sheet::Tools:
      return 0;
    case Sheet::Size:
      return 1;
    case Sheet::Palette:
      return 2;
    case Sheet::Menu:
      return 5;
    default:
      return -1;
  }
}

constexpr int kPaletteCount = 24;
constexpr int kBrushSizeCount = 7;

extern const uint16_t kPalette[kPaletteCount];
extern const uint8_t kBrushSizes[kBrushSizeCount];

}  // namespace ui_layout

class Ui {
 public:
  void begin(uint32_t nowMs);
  void update(const TouchFrame& frame, uint32_t nowMs);
  void render(uint16_t* fb, int fbWidth, const Rect& clip);

  bool dirty() const { return !dirty_.empty(); }
  int dirtyCount() const { return dirty_.count(); }
  Rect dirtyRect(int index) const { return dirty_.rect(index); }
  void clearDirty() { dirty_.reset(); }
  Rect takeDirty() { return dirty_.take(); }

  Screen screen() const { return screen_; }
  Sheet sheet() const { return sheet_; }
  Tool tool() const { return tool_; }
  uint8_t brushSize() const { return brushSize_; }
  uint16_t color() const { return color_; }

  // The main loop applies and persists a completed calibration, then calls
  // calibrationFinished() so the UI can show the result.
  void setTouchCalibrated(bool calibrated) { touchCalibrated_ = calibrated; }
  bool takeCalibration(Calibration& out) {
    if (!calibrationComplete_) {
      return false;
    }
    out = calibrationResult_;
    calibrationComplete_ = false;
    return true;
  }
  void calibrationFinished(bool saved, uint32_t nowMs);

 private:
  void setScreen(Screen next);
  void setSheet(Sheet next);
  void showToast(const char* text, uint32_t nowMs);
  void activateTopBar(int index, uint32_t nowMs);
  void handleCanvasTap(Point p, uint32_t nowMs);
  void activateSheetItem(Point p, uint32_t nowMs);
  void updatePlaceholder(const TouchFrame& frame);
  void startCalibration();
  void updateCalibration(const TouchFrame& frame, uint32_t nowMs);

  void drawSplash(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawCanvasScreen(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawTopBar(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawSheet(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawPlaceholder(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawCalibration(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawToast(uint16_t* fb, int fbWidth, const Rect& clip);

  Screen screen_ = Screen::Splash;
  Sheet sheet_ = Sheet::None;
  Tool tool_ = Tool::Brush;
  uint8_t brushSize_ = config::kDefaultBrushSize;
  uint16_t color_ = 0x0000;
  int8_t pressedButton_ = -1;
  bool pressedBack_ = false;
  bool pressedCalibrate_ = false;
  bool touchCalibrated_ = false;
  bool toastActive_ = false;
  uint32_t toastUntil_ = 0;
  uint32_t splashUntil_ = 0;
  uint8_t calibrationStep_ = 0;
  CalibrationPoint calibrationPoints_[4] = {};
  Calibration calibrationResult_;
  bool calibrationComplete_ = false;
  char toast_[40] = {};
  DirtyRegion dirty_;
};

}  // namespace paint
