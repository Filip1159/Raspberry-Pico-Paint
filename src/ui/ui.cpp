#include "ui/ui.h"

#include <cstdio>

#include "display/gfx.h"
#include "paint/color.h"
#include "version.h"

namespace paint {
namespace ui_layout {

const uint16_t kPalette[kPaletteCount] = {
    rgb565(0, 0, 0),       rgb565(64, 64, 64),    rgb565(128, 128, 128),
    rgb565(192, 192, 192), rgb565(255, 255, 255), rgb565(128, 64, 0),
    rgb565(255, 0, 0),     rgb565(255, 128, 0),   rgb565(255, 255, 0),
    rgb565(128, 255, 0),   rgb565(0, 255, 0),     rgb565(0, 255, 128),
    rgb565(0, 255, 255),   rgb565(0, 128, 255),   rgb565(0, 0, 255),
    rgb565(128, 0, 255),   rgb565(255, 0, 255),   rgb565(255, 0, 128),
    rgb565(128, 0, 0),     rgb565(0, 128, 128),   rgb565(0, 128, 0),
    rgb565(0, 64, 128),    rgb565(64, 0, 128),    rgb565(128, 0, 64),
};

const uint8_t kBrushSizes[kBrushSizeCount] = {1, 2, 4, 8, 16, 24, 32};

}  // namespace ui_layout

namespace {

constexpr uint16_t kChrome = rgb565(28, 28, 34);
constexpr uint16_t kChromeLine = rgb565(64, 64, 72);
constexpr uint16_t kButton = rgb565(48, 48, 58);
constexpr uint16_t kButtonPressed = rgb565(78, 78, 92);
constexpr uint16_t kButtonActive = rgb565(0, 78, 104);
constexpr uint16_t kButtonBorder = rgb565(80, 80, 90);
constexpr uint16_t kAccent = rgb565(0, 170, 220);
constexpr uint16_t kSheet = rgb565(38, 38, 46);
constexpr uint16_t kSheetItem = rgb565(54, 54, 64);
constexpr uint16_t kSheetItemActive = rgb565(0, 96, 128);
constexpr uint16_t kText = kWhite;
constexpr uint16_t kTextDim = rgb565(160, 160, 170);
constexpr uint16_t kDisabled = rgb565(105, 105, 115);
constexpr uint16_t kToastBg = rgb565(20, 20, 26);
constexpr uint16_t kSplashBg = rgb565(12, 12, 18);

const char* const kToolNames[7] = {"Brush", "Eraser", "Line", "Rect", "Ellipse", "Fill", "Picker"};
const char* const kMenuNames[6] = {"New canvas", "Save", "Gallery", "Settings", "Wi-Fi & OTA", "About"};

void drawTextCentered(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& area,
                      const char* text, uint16_t color, uint8_t scale) {
  if (!text) {
    return;
  }
  const int textW = gfx::textWidth(text, scale);
  const int textH = 8 * scale;
  const int x = area.x + (area.w - textW) / 2;
  const int y = area.y + (area.h - textH) / 2;
  gfx::drawText(fb, fbWidth, clip, {static_cast<int16_t>(x), static_cast<int16_t>(y)},
                text, color, scale);
}

void drawTextLeftCentered(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& area,
                          const char* text, uint16_t color, uint8_t scale, int inset) {
  if (!text) {
    return;
  }
  const int textH = 8 * scale;
  const int x = area.x + inset;
  const int y = area.y + (area.h - textH) / 2;
  gfx::drawText(fb, fbWidth, clip, {static_cast<int16_t>(x), static_cast<int16_t>(y)},
                text, color, scale);
}

void drawButtonFrame(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& r,
                     bool pressed, bool active, bool disabled) {
  const uint16_t fill = pressed ? kButtonPressed : (active ? kButtonActive : kButton);
  const uint16_t border = active ? kAccent : (disabled ? kDisabled : kButtonBorder);
  gfx::fillRect(fb, fbWidth, clip, r, fill);
  gfx::drawRect(fb, fbWidth, clip, r, border);
}

void drawArrowIcon(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& r,
                   bool right, uint16_t color) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const int dir = right ? 1 : -1;
  const int headX = cx + dir * 8;
  const int tailX = cx - dir * 8;

  gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(tailX), static_cast<int16_t>(cy - 6)},
                {static_cast<int16_t>(headX), static_cast<int16_t>(cy - 6)}, color);
  gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(headX), static_cast<int16_t>(cy - 6)},
                {static_cast<int16_t>(headX - dir * 6), static_cast<int16_t>(cy - 12)}, color);
  gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(headX), static_cast<int16_t>(cy - 6)},
                {static_cast<int16_t>(headX - dir * 6), static_cast<int16_t>(cy)}, color);
  gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(tailX), static_cast<int16_t>(cy - 6)},
                {static_cast<int16_t>(tailX), static_cast<int16_t>(cy + 4)}, color);
}

void drawMenuIcon(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& r, uint16_t color) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  for (int i = -1; i <= 1; ++i) {
    gfx::fillRect(fb, fbWidth, clip,
                  {static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy + i * 6 - 1), 16, 2},
                  color);
  }
}

void drawSizeIcon(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& r,
                  uint8_t size, uint16_t color) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  gfx::drawCircle(fb, fbWidth, clip, {static_cast<int16_t>(cx), static_cast<int16_t>(cy)}, 10, color);
  const int inner = 2 + size / 8;
  gfx::fillCircle(fb, fbWidth, clip, {static_cast<int16_t>(cx), static_cast<int16_t>(cy)}, inner, color);
}

void drawColorIcon(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& r, uint16_t color) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  gfx::fillCircle(fb, fbWidth, clip, {static_cast<int16_t>(cx), static_cast<int16_t>(cy)}, 9, color);
  gfx::drawCircle(fb, fbWidth, clip, {static_cast<int16_t>(cx), static_cast<int16_t>(cy)}, 10, kWhite);
}

void drawToolIcon(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& r,
                  Tool tool, uint16_t color) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;

  switch (tool) {
    case Tool::Brush:
      for (int offset = -1; offset <= 1; ++offset) {
        gfx::drawLine(fb, fbWidth, clip,
                      {static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy + 8 + offset)},
                      {static_cast<int16_t>(cx + 8), static_cast<int16_t>(cy - 8 + offset)}, color);
      }
      gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(cx + 8), static_cast<int16_t>(cy - 8)},
                    {static_cast<int16_t>(cx + 12), static_cast<int16_t>(cy - 4)}, color);
      gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(cx + 8), static_cast<int16_t>(cy - 8)},
                    {static_cast<int16_t>(cx + 4), static_cast<int16_t>(cy - 12)}, color);
      gfx::fillRect(fb, fbWidth, clip,
                    {static_cast<int16_t>(cx - 10), static_cast<int16_t>(cy + 10), 3, 3}, color);
      break;
    case Tool::Eraser:
      gfx::drawRect(fb, fbWidth, clip,
                    {static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy - 6), 18, 12}, color);
      gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy + 6)},
                    {static_cast<int16_t>(cx + 9), static_cast<int16_t>(cy - 6)}, color);
      break;
    case Tool::Line:
      gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy + 9)},
                    {static_cast<int16_t>(cx + 9), static_cast<int16_t>(cy - 9)}, color);
      break;
    case Tool::Rect:
      gfx::drawRect(fb, fbWidth, clip,
                    {static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy - 7), 18, 14}, color);
      break;
    case Tool::Ellipse:
      gfx::drawCircle(fb, fbWidth, clip, {static_cast<int16_t>(cx), static_cast<int16_t>(cy)}, 9, color);
      break;
    case Tool::Fill:
      gfx::fillRect(fb, fbWidth, clip,
                    {static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy + 2), 12, 6}, color);
      gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(cx - 4), static_cast<int16_t>(cy + 2)},
                    {static_cast<int16_t>(cx + 6), static_cast<int16_t>(cy - 8)}, color);
      gfx::fillCircle(fb, fbWidth, clip,
                      {static_cast<int16_t>(cx + 9), static_cast<int16_t>(cy + 9)}, 2, color);
      break;
    case Tool::Picker:
      gfx::drawLine(fb, fbWidth, clip, {static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy + 8)},
                    {static_cast<int16_t>(cx + 6), static_cast<int16_t>(cy - 6)}, color);
      gfx::drawCircle(fb, fbWidth, clip,
                      {static_cast<int16_t>(cx + 8), static_cast<int16_t>(cy - 8)}, 3, color);
      gfx::fillCircle(fb, fbWidth, clip,
                      {static_cast<int16_t>(cx + 8), static_cast<int16_t>(cy - 8)}, 1, color);
      break;
  }
}

bool rectContains(const Rect& outer, const Rect& inner) {
  return !outer.empty() && !inner.empty() && inner.x >= outer.x &&
         inner.y >= outer.y && inner.right() <= outer.right() &&
         inner.bottom() <= outer.bottom();
}

void drawHsvHueBar(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& bar,
                   uint8_t hue) {
  constexpr int segments = 32;
  for (int i = 0; i < segments; ++i) {
    const int x0 = bar.x + (i * bar.w) / segments;
    const int x1 = bar.x + ((i + 1) * bar.w) / segments;
    const Rect cell{static_cast<int16_t>(x0), bar.y,
                    static_cast<int16_t>(x1 - x0), bar.h};
    if (cell.intersect(clip).empty()) {
      continue;
    }
    const uint8_t h = static_cast<uint8_t>((i * 255) / (segments - 1));
    gfx::fillRect(fb, fbWidth, clip, cell, hsvToRgb565(h, 255, 255));
  }
  gfx::drawRect(fb, fbWidth, clip, bar, kButtonBorder);

  const int markerX = bar.x + (hue * (bar.w - 1)) / 255;
  gfx::vline(fb, fbWidth, clip, markerX, bar.y - 3, bar.h + 6, kWhite);
  gfx::vline(fb, fbWidth, clip, markerX + 1, bar.y - 3, bar.h + 6, kBlack);
}

void drawHsvSquare(uint16_t* fb, int fbWidth, const Rect& clip, const Rect& square,
                   uint8_t hue, uint8_t saturation, uint8_t value) {
  constexpr int columns = 64;
  constexpr int rows = 48;
  for (int row = 0; row < rows; ++row) {
    const int y0 = square.y + (row * square.h) / rows;
    const int y1 = square.y + ((row + 1) * square.h) / rows;
    const uint8_t v = static_cast<uint8_t>(255 - (row * 255) / (rows - 1));
    for (int column = 0; column < columns; ++column) {
      const int x0 = square.x + (column * square.w) / columns;
      const int x1 = square.x + ((column + 1) * square.w) / columns;
      const Rect cell{static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                      static_cast<int16_t>(x1 - x0),
                      static_cast<int16_t>(y1 - y0)};
      if (cell.intersect(clip).empty()) {
        continue;
      }
      const uint8_t s =
          static_cast<uint8_t>((column * 255) / (columns - 1));
      gfx::fillRect(fb, fbWidth, clip, cell, hsvToRgb565(hue, s, v));
    }
  }
  gfx::drawRect(fb, fbWidth, clip, square, kButtonBorder);

  const int markerX = square.x + (saturation * (square.w - 1)) / 255;
  const int markerY = square.y + ((255 - value) * (square.h - 1)) / 255;
  gfx::drawCircle(fb, fbWidth, clip,
                  {static_cast<int16_t>(markerX), static_cast<int16_t>(markerY)}, 7,
                  kWhite);
  gfx::drawCircle(fb, fbWidth, clip,
                  {static_cast<int16_t>(markerX), static_cast<int16_t>(markerY)}, 8,
                  kBlack);
}

}  // namespace

void Ui::begin(uint32_t nowMs) {
  screen_ = Screen::Splash;
  sheet_ = Sheet::None;
  tool_ = Tool::Brush;
  brushSize_ = config::kDefaultBrushSize;
  color_ = 0x0000;
  pressedButton_ = -1;
  pressedBack_ = false;
  pressedCalibrate_ = false;
  touchCalibrated_ = false;
  calibrationStep_ = 0;
  calibrationComplete_ = false;
  toastActive_ = false;
  undoAvailable_ = false;
  redoAvailable_ = false;
  undoRequested_ = false;
  redoRequested_ = false;
  newCanvasRequested_ = false;
  hsvH_ = 0;
  hsvS_ = 0;
  hsvV_ = 0;
  recentCount_ = 0;
  for (int i = 0; i < ui_layout::kRecentColorCount; ++i) {
    recentColors_[i] = 0;
  }
  toastUntil_ = 0;
  splashUntil_ = nowMs + 800;
  toast_[0] = '\0';
  dirty_.reset();
  dirty_.add(ui_layout::kScreen);
}

void Ui::setScreen(Screen next) {
  if (next == screen_) {
    return;
  }
  screen_ = next;
  sheet_ = Sheet::None;
  pressedButton_ = -1;
  pressedBack_ = false;
  pressedCalibrate_ = false;
  // Do not carry a toast from the canvas over a placeholder screen.
  toastActive_ = false;
  dirty_.add(ui_layout::kScreen);
}

void Ui::setSheet(Sheet next) {
  if (next == sheet_) {
    return;
  }
  if (toastActive_) {
    toastActive_ = false;
    dirty_.add(ui_layout::kToast);
  }
  // Only the button(s) whose active state changes need repainting. Marking the
  // whole 320x48 top bar costs ~14 ms on hardware; a 44x44 button is ~1.9 ms.
  const int oldButton = ui_layout::sheetButton(sheet_);
  const int newButton = ui_layout::sheetButton(next);
  if (oldButton >= 0) {
    dirty_.add(ui_layout::kButtons[oldButton]);
  }
  if (newButton >= 0) {
    dirty_.add(ui_layout::kButtons[newButton]);
  }
  const Rect oldRect = ui_layout::sheetRect(sheet_);
  const Rect newRect = ui_layout::sheetRect(next);
  sheet_ = next;
  if (oldRect == newRect) {
    if (!newRect.empty()) {
      dirty_.add(newRect);
    }
  } else {
    // When one sheet fully covers the other, repainting only the larger rect is
    // enough. This avoids a redundant second pass when Palette opens HSV (the
    // HSV sheet contains the palette) and when HSV returns to Palette.
    const bool oldInsideNew = rectContains(newRect, oldRect);
    const bool newInsideOld = rectContains(oldRect, newRect);
    if (!oldRect.empty() && !oldInsideNew) {
      dirty_.add(oldRect);
    }
    if (!newRect.empty() && !newInsideOld) {
      dirty_.add(newRect);
    }
  }
}

void Ui::showToast(const char* text, uint32_t nowMs) {
  std::snprintf(toast_, sizeof(toast_), "%s", text ? text : "");
  toastActive_ = true;
  toastUntil_ = nowMs + 2000;
  dirty_.add(ui_layout::kToast);
}

void Ui::activateTopBar(int index, uint32_t nowMs) {
  switch (index) {
    case 0:
      setSheet(sheet_ == Sheet::Tools ? Sheet::None : Sheet::Tools);
      break;
    case 1:
      setSheet(sheet_ == Sheet::Size ? Sheet::None : Sheet::Size);
      break;
    case 2:
      setSheet(sheet_ == Sheet::Palette ? Sheet::None : Sheet::Palette);
      break;
    case 3:
      if (undoAvailable_) {
        setSheet(Sheet::None);
        undoRequested_ = true;
      } else {
        showToast("Nothing to undo", nowMs);
      }
      break;
    case 4:
      if (redoAvailable_) {
        setSheet(Sheet::None);
        redoRequested_ = true;
      } else {
        showToast("Nothing to redo", nowMs);
      }
      break;
    case 5:
      setSheet(sheet_ == Sheet::Menu ? Sheet::None : Sheet::Menu);
      break;
    default:
      break;
  }
}

void Ui::handleCanvasTap(Point p, uint32_t nowMs) {
  if (sheet_ == Sheet::None) {
    return;
  }

  if (ui_layout::sheetRect(sheet_).contains(p)) {
    activateSheetItem(p, nowMs);
  } else {
    setSheet(Sheet::None);
  }
}

void Ui::activateSheetItem(Point p, uint32_t nowMs) {
  const int item = ui_layout::hitSheetItem(sheet_, p);

  switch (sheet_) {
    case Sheet::Tools:
      if (item >= 0 && item < 7) {
        tool_ = static_cast<Tool>(item);
        dirty_.add(ui_layout::kButtons[0]);
        setSheet(Sheet::None);
        showToast(kToolNames[item], nowMs);
      }
      break;
    case Sheet::Palette: {
      const int recent = ui_layout::hitPaletteRecent(p);
      if (ui_layout::hitPaletteHsv(p)) {
        openHsv();
      } else if (recent >= 0 && recent < recentCount_) {
        color_ = recentColors_[recent];
        rememberColor(color_);
        dirty_.add(ui_layout::kButtons[2]);
        setSheet(Sheet::None);
        showToast("Colour selected", nowMs);
      } else if (item >= 0 && item < ui_layout::kPaletteCount) {
        color_ = ui_layout::kPalette[item];
        rememberColor(color_);
        dirty_.add(ui_layout::kButtons[2]);
        setSheet(Sheet::None);
        showToast("Colour selected", nowMs);
      }
      break;
    }
    case Sheet::Size:
      if (item >= 0 && item < ui_layout::kBrushSizeCount) {
        brushSize_ = ui_layout::kBrushSizes[item];
        dirty_.add(ui_layout::kButtons[1]);
        setSheet(Sheet::None);
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "Brush %u px", static_cast<unsigned>(brushSize_));
        showToast(buffer, nowMs);
      }
      break;
    case Sheet::Menu:
      if (item >= 0 && item < 6) {
        if (item == 0) {
          setSheet(Sheet::ConfirmNew);
        } else {
          setSheet(Sheet::None);
          switch (item) {
            case 1:
              showToast("Save lands in M6", nowMs);
              break;
            case 2:
              setScreen(Screen::Gallery);
              break;
            case 3:
              setScreen(Screen::Settings);
              break;
            case 4:
              showToast("Wi-Fi & OTA lands in M7", nowMs);
              break;
            case 5:
              setScreen(Screen::About);
              break;
            default:
              break;
          }
        }
      }
      break;
    case Sheet::ConfirmNew:
      if (ui_layout::kConfirmNewCancel.contains(p)) {
        setSheet(Sheet::None);
      } else if (ui_layout::kConfirmNewOk.contains(p)) {
        setSheet(Sheet::None);
        newCanvasRequested_ = true;
      }
      break;
    case Sheet::Hsv:
      if (ui_layout::kHsvCancel.contains(p)) {
        setSheet(Sheet::None);
      } else if (ui_layout::kHsvOk.contains(p)) {
        color_ = hsvToRgb565(hsvH_, hsvS_, hsvV_);
        rememberColor(color_);
        dirty_.add(ui_layout::kButtons[2]);
        setSheet(Sheet::None);
        showToast("Colour selected", nowMs);
      } else if (ui_layout::kHsvHueBar.contains(p)) {
        int hue = ((p.x - ui_layout::kHsvHueBar.x) * 255) /
                  (ui_layout::kHsvHueBar.w - 1);
        if (hue < 0) {
          hue = 0;
        }
        if (hue > 255) {
          hue = 255;
        }
        hsvH_ = static_cast<uint8_t>(hue);
        dirty_.add(ui_layout::kHsvHuePaint);
        dirty_.add(ui_layout::kHsvSquarePaint);
        dirty_.add(ui_layout::kHsvPreview);
        dirty_.add(ui_layout::kHsvReadout);
      } else if (ui_layout::kHsvSquare.contains(p)) {
        int saturation = ((p.x - ui_layout::kHsvSquare.x) * 255) /
                         (ui_layout::kHsvSquare.w - 1);
        int value = 255 - ((p.y - ui_layout::kHsvSquare.y) * 255) /
                               (ui_layout::kHsvSquare.h - 1);
        if (saturation < 0) {
          saturation = 0;
        }
        if (saturation > 255) {
          saturation = 255;
        }
        if (value < 0) {
          value = 0;
        }
        if (value > 255) {
          value = 255;
        }
        hsvS_ = static_cast<uint8_t>(saturation);
        hsvV_ = static_cast<uint8_t>(value);
        dirty_.add(ui_layout::kHsvSquarePaint);
        dirty_.add(ui_layout::kHsvPreview);
        dirty_.add(ui_layout::kHsvReadout);
      }
      break;
    default:
      break;
  }
}

void Ui::openHsv() {
  const Hsv hsv = rgb565ToHsv(color_);
  hsvH_ = hsv.h;
  hsvS_ = hsv.s;
  hsvV_ = hsv.v;
  setSheet(Sheet::Hsv);
}

void Ui::rememberColor(uint16_t color) {
  int existing = -1;
  for (int i = 0; i < recentCount_; ++i) {
    if (recentColors_[i] == color) {
      existing = i;
      break;
    }
  }
  if (existing == 0) {
    return;
  }

  int last = recentCount_;
  if (existing > 0) {
    last = existing;
  } else if (recentCount_ < ui_layout::kRecentColorCount) {
    ++recentCount_;
    last = recentCount_ - 1;
  } else {
    last = ui_layout::kRecentColorCount - 1;
  }

  for (int i = last; i > 0; --i) {
    recentColors_[i] = recentColors_[i - 1];
  }
  recentColors_[0] = color;
}

void Ui::updatePlaceholder(const TouchFrame& frame) {
  const Point p{frame.x, frame.y};

  if (frame.down) {
    if (ui_layout::hitBackButton(p)) {
      pressedBack_ = true;
      dirty_.add(ui_layout::kBackButton);
    } else {
      pressedBack_ = false;
    }
  }

  if (frame.move && pressedBack_ && !ui_layout::hitBackButton(p)) {
    pressedBack_ = false;
    dirty_.add(ui_layout::kBackButton);
  }

  if (frame.up) {
    const bool wasPressed = pressedBack_;
    pressedBack_ = false;
    if (wasPressed) {
      dirty_.add(ui_layout::kBackButton);
      if (ui_layout::hitBackButton(p)) {
        setScreen(Screen::Canvas);
        return;
      }
    }
  }

  if (screen_ != Screen::Settings) {
    return;
  }

  if (frame.down) {
    if (ui_layout::kCalibrateButton.contains(p)) {
      pressedCalibrate_ = true;
      dirty_.add(ui_layout::kCalibrateButton);
    } else {
      pressedCalibrate_ = false;
    }
  }

  if (frame.move && pressedCalibrate_ &&
      !ui_layout::kCalibrateButton.contains(p)) {
    pressedCalibrate_ = false;
    dirty_.add(ui_layout::kCalibrateButton);
  }

  if (frame.up) {
    const bool wasPressed = pressedCalibrate_;
    pressedCalibrate_ = false;
    if (wasPressed) {
      dirty_.add(ui_layout::kCalibrateButton);
      if (ui_layout::kCalibrateButton.contains(p)) {
        startCalibration();
      }
    }
  }
}

void Ui::startCalibration() {
  calibrationStep_ = 0;
  calibrationComplete_ = false;
  calibrationResult_ = Calibration{};
  for (CalibrationPoint& point : calibrationPoints_) {
    point = CalibrationPoint{};
  }
  setScreen(Screen::Calibrate);
}

void Ui::updateCalibration(const TouchFrame& frame, uint32_t nowMs) {
  if (!frame.down) {
    return;
  }

  const Point p{frame.x, frame.y};
  if (ui_layout::kCalibrationCancel.contains(p)) {
    setScreen(Screen::Settings);
    return;
  }
  if (calibrationStep_ >= 4) {
    return;
  }

  const Point target = ui_layout::calibrationTarget(calibrationStep_);
  const int32_t dx = static_cast<int32_t>(p.x) - target.x;
  const int32_t dy = static_cast<int32_t>(p.y) - target.y;
  if (dx * dx + dy * dy >
      ui_layout::kCalibrationHitRadius * ui_layout::kCalibrationHitRadius) {
    showToast("Touch the crosshair", nowMs);
    return;
  }
  if (frame.rawX == 0 && frame.rawY == 0) {
    showToast("Touch not read; try again", nowMs);
    return;
  }

  calibrationPoints_[calibrationStep_] = {
      static_cast<int32_t>(frame.rawX), static_cast<int32_t>(frame.rawY),
      target.x, target.y};
  ++calibrationStep_;

  if (calibrationStep_ == 4) {
    calibrationResult_ = makeFourPointCalibration(calibrationPoints_);
    if (isCalibrationPlausible(calibrationResult_) &&
        calibrationMatchesPoints(calibrationResult_, calibrationPoints_)) {
      calibrationComplete_ = true;
    } else {
      calibrationStep_ = 0;
      showToast("Calibration failed - try again", nowMs);
    }
  }
  dirty_.add(ui_layout::kScreen);
}

void Ui::calibrationFinished(bool saved, uint32_t nowMs) {
  touchCalibrated_ = true;
  setScreen(Screen::Settings);
  showToast(saved ? "Calibration saved" : "Calibration applied - save failed", nowMs);
}

void Ui::update(const TouchFrame& frame, uint32_t nowMs) {
  if (screen_ == Screen::Splash) {
    if (frame.down || static_cast<int32_t>(nowMs - splashUntil_) >= 0) {
      screen_ = Screen::Canvas;
      sheet_ = Sheet::None;
      pressedButton_ = -1;
      pressedBack_ = false;
      dirty_.add(ui_layout::kScreen);
    }
    return;
  }

  if (toastActive_ && static_cast<int32_t>(nowMs - toastUntil_) >= 0) {
    toastActive_ = false;
    dirty_.add(ui_layout::kToast);
  }

  if (screen_ == Screen::Calibrate) {
    updateCalibration(frame, nowMs);
    return;
  }

  if (screen_ != Screen::Canvas) {
    updatePlaceholder(frame);
    return;
  }

  if (frame.down) {
    const int button = ui_layout::hitTopBarButton({frame.x, frame.y});
    if (button >= 0) {
      pressedButton_ = static_cast<int8_t>(button);
      dirty_.add(ui_layout::kButtons[button]);
    } else {
      pressedButton_ = -1;
    }
  }

  if (frame.move && pressedButton_ >= 0) {
    const int button = ui_layout::hitTopBarButton({frame.x, frame.y});
    if (button != pressedButton_) {
      dirty_.add(ui_layout::kButtons[pressedButton_]);
      pressedButton_ = -1;
    }
  }

  if (frame.up) {
    if (pressedButton_ >= 0) {
      const int oldButton = pressedButton_;
      pressedButton_ = -1;
      dirty_.add(ui_layout::kButtons[oldButton]);
      const int button = ui_layout::hitTopBarButton({frame.x, frame.y});
      if (button == oldButton) {
        activateTopBar(oldButton, nowMs);
      }
      return;
    }

    if (frame.tap) {
      handleCanvasTap({frame.x, frame.y}, nowMs);
    }
  }
}

void Ui::render(uint16_t* fb, int fbWidth, const Rect& clip) {
  if (!fb || dirty_.empty()) {
    return;
  }
  compose(fb, fbWidth, clip);
}

void Ui::compose(uint16_t* fb, int fbWidth, const Rect& clip) {
  if (!fb) {
    return;
  }

  switch (screen_) {
    case Screen::Splash:
      drawSplash(fb, fbWidth, clip);
      break;
    case Screen::Canvas:
      drawCanvasScreen(fb, fbWidth, clip);
      break;
    case Screen::Gallery:
    case Screen::Settings:
    case Screen::About:
    case Screen::Recovery:
      drawPlaceholder(fb, fbWidth, clip);
      break;
    case Screen::Calibrate:
      drawCalibration(fb, fbWidth, clip);
      break;
  }

  if (toastActive_ && screen_ != Screen::Splash) {
    drawToast(fb, fbWidth, clip);
  }
}

void Ui::drawSplash(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kSplashBg);
  drawTextCentered(fb, fbWidth, clip, {0, 110, 320, 90}, "Pico Paint", kWhite, 4);
  drawTextCentered(fb, fbWidth, clip, {0, 205, 320, 30}, "M4 drawing", kAccent, 2);

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "v%s", PAINT_VERSION);
  drawTextCentered(fb, fbWidth, clip, {0, 245, 320, 24}, buffer, kTextDim, 2);
  drawTextCentered(fb, fbWidth, clip, {0, 300, 320, 24}, "Tap to skip", kTextDim, 2);

  constexpr int swatch = 12;
  const int startX = (config::kScreenWidth - ui_layout::kPaletteCount * swatch) / 2;
  for (int i = 0; i < ui_layout::kPaletteCount; ++i) {
    gfx::fillRect(fb, fbWidth, clip,
                  {static_cast<int16_t>(startX + i * swatch), 360, swatch, 10},
                  ui_layout::kPalette[i]);
  }
}

void Ui::drawCanvasScreen(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kChrome);
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kCanvas, config::kPaperColor);
  drawTopBar(fb, fbWidth, clip);
  if (sheet_ != Sheet::None) {
    drawSheet(fb, fbWidth, clip);
  }
}

void Ui::drawTopBar(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kTopBar, kChrome);
  gfx::hline(fb, fbWidth, clip, 0, config::kTopBarHeight - 1, config::kScreenWidth,
             kChromeLine);

  for (int i = 0; i < 6; ++i) {
    const Rect r = ui_layout::kButtons[i];
    const bool active = (i == 0 && sheet_ == Sheet::Tools) ||
                        (i == 1 && sheet_ == Sheet::Size) ||
                        (i == 2 &&
                         (sheet_ == Sheet::Palette || sheet_ == Sheet::Hsv)) ||
                        (i == 5 && sheet_ == Sheet::Menu);
    const bool pressed = pressedButton_ == i;
    const bool disabled = (i == 3 && !undoAvailable_) ||
                          (i == 4 && !redoAvailable_);

    drawButtonFrame(fb, fbWidth, clip, r, pressed, active, disabled);

    switch (i) {
      case 0:
        drawToolIcon(fb, fbWidth, clip, r, tool_, kText);
        break;
      case 1:
        drawSizeIcon(fb, fbWidth, clip, r, brushSize_, kText);
        break;
      case 2:
        drawColorIcon(fb, fbWidth, clip, r, color_);
        break;
      case 3:
        drawArrowIcon(fb, fbWidth, clip, r, false, disabled ? kDisabled : kText);
        break;
      case 4:
        drawArrowIcon(fb, fbWidth, clip, r, true, disabled ? kDisabled : kText);
        break;
      case 5:
        drawMenuIcon(fb, fbWidth, clip, r, kText);
        break;
      default:
        break;
    }
  }
}

void Ui::drawSheet(uint16_t* fb, int fbWidth, const Rect& clip) {
  const Rect sheet = ui_layout::sheetRect(sheet_);
  if (sheet.empty()) {
    return;
  }

  gfx::fillRect(fb, fbWidth, clip, sheet, kSheet);
  gfx::fillRect(fb, fbWidth, clip,
                {sheet.x, sheet.y, sheet.w, 3}, kAccent);
  gfx::hline(fb, fbWidth, clip, sheet.x, static_cast<int16_t>(sheet.y + 3), sheet.w,
             kChromeLine);

  switch (sheet_) {
    case Sheet::Tools:
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 8)},
                    "Tools", kText, 2);
      for (int i = 0; i < 7; ++i) {
        const Rect r = ui_layout::toolItemRect(i);
        const bool active = tool_ == static_cast<Tool>(i);
        gfx::fillRect(fb, fbWidth, clip, r, active ? kSheetItemActive : kSheetItem);
        gfx::drawRect(fb, fbWidth, clip, r, active ? kAccent : kButtonBorder);
        drawTextLeftCentered(fb, fbWidth, clip, r, kToolNames[i], kText, 2, 8);
      }
      break;

    case Sheet::Palette:
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 8)},
                    "Colour", kText, 2);
      {
        const Rect hsv = ui_layout::kPaletteHsvButton;
        gfx::fillRect(fb, fbWidth, clip, hsv, kButton);
        gfx::drawRect(fb, fbWidth, clip, hsv, kButtonBorder);
        drawTextCentered(fb, fbWidth, clip, hsv, "HSV", kText, 2);
      }
      for (int i = 0; i < recentCount_; ++i) {
        const Rect r = ui_layout::paletteRecentRect(i);
        gfx::fillRect(fb, fbWidth, clip, r, recentColors_[i]);
        gfx::drawRect(fb, fbWidth, clip, r,
                      color_ == recentColors_[i] ? kWhite : kButtonBorder);
      }
      for (int i = 0; i < ui_layout::kPaletteCount; ++i) {
        const Rect r = ui_layout::paletteSwatchRect(i);
        gfx::fillRect(fb, fbWidth, clip, r, ui_layout::kPalette[i]);
        gfx::drawRect(fb, fbWidth, clip, r, kButtonBorder);
        if (color_ == ui_layout::kPalette[i]) {
          gfx::drawRect(fb, fbWidth, clip,
                        {static_cast<int16_t>(r.x - 2), static_cast<int16_t>(r.y - 2),
                         static_cast<int16_t>(r.w + 4), static_cast<int16_t>(r.h + 4)},
                        kWhite);
        }
      }
      break;

    case Sheet::Size: {
      char title[32];
      std::snprintf(title, sizeof(title), "Brush size: %u px",
                    static_cast<unsigned>(brushSize_));
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 8)},
                    title, kText, 2);
      for (int i = 0; i < ui_layout::kBrushSizeCount; ++i) {
        const Rect r = ui_layout::sizeItemRect(i);
        const uint8_t size = ui_layout::kBrushSizes[i];
        const bool active = brushSize_ == size;
        gfx::fillRect(fb, fbWidth, clip, r, active ? kSheetItemActive : kSheetItem);
        gfx::drawRect(fb, fbWidth, clip, r, active ? kAccent : kButtonBorder);
        gfx::fillCircle(fb, fbWidth, clip,
                        {static_cast<int16_t>(r.x + r.w / 2),
                         static_cast<int16_t>(r.y + r.h / 2)},
                        size / 2, kWhite);
      }
      break;
    }

    case Sheet::Menu:
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 8)},
                    "Menu", kText, 2);
      for (int i = 0; i < 6; ++i) {
        const Rect r = ui_layout::menuItemRect(i);
        gfx::fillRect(fb, fbWidth, clip, r, kSheetItem);
        gfx::drawRect(fb, fbWidth, clip, r, kButtonBorder);
        drawTextLeftCentered(fb, fbWidth, clip, r, kMenuNames[i], kText, 2, 10);
      }
      break;

    case Sheet::ConfirmNew:
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 12)},
                    "New canvas?", kText, 2);
      gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(sheet.y + 54)},
                    "Clear the drawing and", kTextDim, 2);
      gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(sheet.y + 78)},
                    "undo history?", kTextDim, 2);
      gfx::fillRect(fb, fbWidth, clip, ui_layout::kConfirmNewCancel, kButton);
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kConfirmNewCancel, kButtonBorder);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kConfirmNewCancel, "Cancel",
                       kText, 2);
      gfx::fillRect(fb, fbWidth, clip, ui_layout::kConfirmNewOk, kButtonActive);
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kConfirmNewOk, kAccent);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kConfirmNewOk, "Clear",
                       kText, 2);
      break;

    case Sheet::Hsv: {
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 8)},
                    "HSV colour", kText, 2);

      gfx::fillRect(fb, fbWidth, clip, ui_layout::kHsvPreview,
                    hsvToRgb565(hsvH_, hsvS_, hsvV_));
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kHsvPreview, kWhite);

      // The readout is dirty separately from the rest of the sheet, so clear
      // its background before drawing the fixed-width values.
      gfx::fillRect(fb, fbWidth, clip, ui_layout::kHsvReadout, kSheet);
      char line[32];
      std::snprintf(line, sizeof(line), "H %3u",
                    static_cast<unsigned>((hsvH_ * 360u) / 255u));
      gfx::drawText(fb, fbWidth, clip, {88, static_cast<int16_t>(sheet.y + 44)},
                    line, kText, 2);
      std::snprintf(line, sizeof(line), "S %3u%%  V %3u%%",
                    static_cast<unsigned>((hsvS_ * 100u) / 255u),
                    static_cast<unsigned>((hsvV_ * 100u) / 255u));
      gfx::drawText(fb, fbWidth, clip, {88, static_cast<int16_t>(sheet.y + 68)},
                    line, kText, 2);

      gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(sheet.y + 94)},
                    "Hue", kTextDim, 2);
      drawHsvHueBar(fb, fbWidth, clip, ui_layout::kHsvHueBar, hsvH_);

      gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(sheet.y + 138)},
                    "Sat / Val", kTextDim, 2);
      drawHsvSquare(fb, fbWidth, clip, ui_layout::kHsvSquare, hsvH_, hsvS_, hsvV_);

      gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(sheet.y + 300)},
                    "Tap a control to adjust", kTextDim, 2);

      gfx::fillRect(fb, fbWidth, clip, ui_layout::kHsvCancel, kButton);
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kHsvCancel, kButtonBorder);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kHsvCancel, "Cancel", kText,
                       2);
      gfx::fillRect(fb, fbWidth, clip, ui_layout::kHsvOk, kButtonActive);
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kHsvOk, kAccent);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kHsvOk, "Use", kText, 2);
      break;
    }

    default:
      break;
  }
}

void Ui::drawPlaceholder(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kSplashBg);
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kTopBar, kChrome);
  gfx::hline(fb, fbWidth, clip, 0, config::kTopBarHeight - 1, config::kScreenWidth,
             kChromeLine);

  drawButtonFrame(fb, fbWidth, clip, ui_layout::kBackButton, pressedBack_, false, false);
  drawArrowIcon(fb, fbWidth, clip, ui_layout::kBackButton, false, kText);

  const char* title = "Gallery";
  switch (screen_) {
    case Screen::Gallery:
      title = "Gallery";
      break;
    case Screen::Settings:
      title = "Settings";
      break;
    case Screen::About:
      title = "About";
      break;
    case Screen::Recovery:
      title = "Recovery";
      break;
    default:
      break;
  }
  gfx::drawText(fb, fbWidth, clip, {80, 12}, title, kText, 3);

  switch (screen_) {
    case Screen::Gallery:
      for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
          const Rect r{static_cast<int16_t>(20 + col * 150),
                       static_cast<int16_t>(80 + row * 180), 130, 160};
          gfx::fillRect(fb, fbWidth, clip, r, kButton);
          gfx::drawRect(fb, fbWidth, clip, r, kButtonBorder);
          drawTextCentered(fb, fbWidth, clip, r, "Empty", kTextDim, 2);
        }
      }
      drawTextCentered(fb, fbWidth, clip, {0, 440, 320, 24}, "No drawings yet",
                       kTextDim, 2);
      break;

    case Screen::Settings: {
      const char* lines[5] = {
          "Rotation: 2 (fixed)",
          "Brightness: 100%",
          touchCalibrated_ ? "Touch: calibrated" : "Touch: factory map",
          "Storage: LittleFS 2 MB",
          "Wi-Fi & OTA: M7",
      };
      for (int i = 0; i < 5; ++i) {
        gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(84 + i * 32)},
                      lines[i], kText, 2);
      }

      drawButtonFrame(fb, fbWidth, clip, ui_layout::kCalibrateButton,
                      pressedCalibrate_, false, false);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kCalibrateButton,
                       "Calibrate touch", kText, 2);
      drawTextCentered(fb, fbWidth, clip, {0, 324, 320, 20},
                       "Touch each corner in order", kTextDim, 2);
      break;
    }

    case Screen::About: {
      gfx::drawText(fb, fbWidth, clip, {20, 80}, "Pico Paint", kWhite, 3);
      char buffer[48];
      std::snprintf(buffer, sizeof(buffer), "Version %s", PAINT_VERSION);
      gfx::drawText(fb, fbWidth, clip, {20, 120}, buffer, kText, 2);
      std::snprintf(buffer, sizeof(buffer), "Built %s", PAINT_BUILD_DATE);
      gfx::drawText(fb, fbWidth, clip, {20, 148}, buffer, kTextDim, 2);
      gfx::drawText(fb, fbWidth, clip, {20, 176}, PAINT_BUILD_TIME, kTextDim, 2);
      gfx::drawText(fb, fbWidth, clip, {20, 216}, "Pico 2 W (RP2350)", kText, 2);
      gfx::drawText(fb, fbWidth, clip, {20, 244}, "ILI9488 320x480", kText, 2);
      gfx::drawText(fb, fbWidth, clip, {20, 272}, "HR2046 touch", kText, 2);
      gfx::drawText(fb, fbWidth, clip, {20, 300}, "LittleFS 2 MB", kText, 2);
      gfx::drawText(fb, fbWidth, clip, {20, 340}, "M4 drawing + undo", kTextDim, 2);
      break;
    }

    case Screen::Recovery:
      drawTextCentered(fb, fbWidth, clip, {0, 200, 320, 24}, "Recovery mode",
                       kText, 3);
      drawTextCentered(fb, fbWidth, clip, {0, 240, 320, 24}, "Hold touch at boot (M8)",
                       kTextDim, 2);
      break;

    default:
      break;
  }
}

void Ui::drawCalibration(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kSplashBg);
  drawTextCentered(fb, fbWidth, clip, {0, 10, 320, 24}, "Touch calibration", kWhite, 2);

  char buffer[48];
  if (calibrationStep_ < 4) {
    static const char* const kCornerNames[4] = {
        "top-left", "top-right", "bottom-left", "bottom-right"};
    std::snprintf(buffer, sizeof(buffer), "Step %u of 4 - %s",
                  static_cast<unsigned>(calibrationStep_ + 1),
                  kCornerNames[calibrationStep_]);
  } else {
    std::snprintf(buffer, sizeof(buffer), "Applying...");
  }
  drawTextCentered(fb, fbWidth, clip, {0, 40, 320, 20}, buffer, kTextDim, 2);

  if (calibrationStep_ < 4) {
    const Point target = ui_layout::calibrationTarget(calibrationStep_);
    constexpr int arm = 16;
    gfx::hline(fb, fbWidth, clip, target.x - arm, target.y, arm * 2 + 1, kAccent);
    gfx::vline(fb, fbWidth, clip, target.x, target.y - arm, arm * 2 + 1, kAccent);
    gfx::drawCircle(fb, fbWidth, clip, target, 12, kWhite);
    gfx::fillCircle(fb, fbWidth, clip, target, 2, kAccent);
  } else {
    drawTextCentered(fb, fbWidth, clip, {0, 210, 320, 24}, "Calibration complete",
                     kAccent, 2);
  }

  drawButtonFrame(fb, fbWidth, clip, ui_layout::kCalibrationCancel, false, false, false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kCalibrationCancel, "Cancel", kText, 2);
}

void Ui::drawToast(uint16_t* fb, int fbWidth, const Rect& clip) {
  if (!toastActive_) {
    return;
  }
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kToast, kToastBg);
  gfx::drawRect(fb, fbWidth, clip, ui_layout::kToast, kAccent);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kToast, toast_, kWhite, 2);
}

}  // namespace paint
