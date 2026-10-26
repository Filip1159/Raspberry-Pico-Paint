#include "ui/ui.h"

#include <cstdio>
#include <cstring>

#include "display/gfx.h"
#include "input/orientation.h"
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
constexpr uint16_t kDanger = rgb565(220, 64, 64);
constexpr uint16_t kToastBg = rgb565(20, 20, 26);
constexpr uint16_t kSplashBg = rgb565(12, 12, 18);
constexpr uint16_t kModalDim = rgb565(8, 8, 12);

const char* const kToolNames[7] = {"Brush", "Eraser", "Line", "Rect", "Ellipse", "Fill", "Picker"};
const char* const kMenuNames[7] = {"New canvas", "Save",      "Save as", "Gallery",
                                   "Settings",   "Wi-Fi & OTA", "About"};

bool isShapeTool(Tool tool) {
  return tool == Tool::Line || tool == Tool::Rect || tool == Tool::Ellipse;
}

bool sameNetInfo(const NetInfo& a, const NetInfo& b) {
  return a.state == b.state && a.enabled == b.enabled &&
         a.configured == b.configured && a.server == b.server &&
         a.lowStorage == b.lowStorage && a.rssi == b.rssi &&
         a.progress == b.progress && a.error == b.error &&
         std::memcmp(a.ip, b.ip, sizeof(a.ip)) == 0;
}

bool sameNetworkStatus(const NetInfo& a, const NetInfo& b) {
  return a.state == b.state && a.enabled == b.enabled &&
         a.configured == b.configured && a.server == b.server &&
         a.lowStorage == b.lowStorage && a.error == b.error &&
         std::memcmp(a.ip, b.ip, sizeof(a.ip)) == 0;
}

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

void drawTextRightCentered(uint16_t* fb, int fbWidth, const Rect& clip,
                           const Rect& area, const char* text, uint16_t color,
                           uint8_t scale, int inset) {
  if (!text) {
    return;
  }
  const int textW = gfx::textWidth(text, scale);
  const int textH = 8 * scale;
  const int x = area.right() - inset - textW;
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
  // 32x32 cells are visually smooth at 200x140 and halve the number of
  // fillRect calls compared with the previous 64x48 grid. This is the dominant
  // cost when a hue tap has to repaint every cell.
  constexpr int columns = 32;
  constexpr int rows = 32;
  for (int row = 0; row < rows; ++row) {
    const int y0 = square.y + (row * square.h) / rows;
    const int y1 = square.y + ((row + 1) * square.h) / rows;
    if (y1 <= clip.y || y0 >= clip.bottom()) {
      continue;
    }
    const uint8_t v = static_cast<uint8_t>(255 - (row * 255) / (rows - 1));
    for (int column = 0; column < columns; ++column) {
      const int x0 = square.x + (column * square.w) / columns;
      const int x1 = square.x + ((column + 1) * square.w) / columns;
      if (x1 <= clip.x || x0 >= clip.right()) {
        continue;
      }
      const Rect cell{static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                      static_cast<int16_t>(x1 - x0),
                      static_cast<int16_t>(y1 - y0)};
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
  shapeFilled_ = false;
  pressedButton_ = -1;
  pressedBack_ = false;
  pressedCalibrate_ = false;
  pressedStorage_ = false;
  pressedNetwork_ = false;
  pressedRotation_ = false;
  pressedBrightnessDown_ = false;
  pressedBrightnessUp_ = false;
  pressedSettingsWifi_ = false;
  pressedAbout_ = false;
  pressedSettingsNetwork_ = false;
  pressedSafeDisplay_ = false;
  pressedSafeTouch_ = false;
  pressedSafeStorage_ = false;
  pressedSafeContinue_ = false;
  pressedSafeReboot_ = false;
  networkToggleRequested_ = false;
  networkInfo_ = NetInfo{};
  displayRotation_ = config::kDisplayRotation;
  brightness_ = config::kDefaultBrightness;
  rotationToggleRequested_ = false;
  brightnessStepRequested_ = false;
  brightnessStep_ = 0;
  safeMode_ = false;
  safeDisplayOk_ = true;
  safeTouchOk_ = true;
  safeStorageOk_ = true;
  safeContinueRequested_ = false;
  safeRebootRequested_ = false;
  touchTestSeen_ = false;
  touchTestX_ = config::kScreenWidth / 2;
  touchTestY_ = config::kScreenHeight / 2;
  touchTestRawX_ = 0;
  touchTestRawY_ = 0;
  otaActive_ = false;
  otaProgress_ = 0;
  touchCalibrated_ = false;
  calibrationStep_ = 0;
  calibrationComplete_ = false;
  toastActive_ = false;
  undoAvailable_ = false;
  redoAvailable_ = false;
  undoRequested_ = false;
  redoRequested_ = false;
  newCanvasRequested_ = false;
  galleryCount_ = 0;
  galleryPage_ = 0;
  gallerySelected_ = -1;
  galleryDialog_ = GalleryDialog::None;
  saveAsRequested_ = false;
  saveRequested_ = false;
  saveName_[0] = '\0';
  loadRequested_ = false;
  loadName_[0] = '\0';
  deleteRequested_ = false;
  deleteName_[0] = '\0';
  documentName_[0] = '\0';
  documentDirty_ = false;
  storageStats_ = StorageStats{};
  storageFileCount_ = 0;
  recoveryAvailable_ = false;
  recoveryRestoreRequested_ = false;
  recoveryDiscardRequested_ = false;
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

void Ui::setGalleryFiles(const char* const* names, int count) {
  if (count < 0) {
    count = 0;
  }
  if (count > kMaxPaintFiles) {
    count = kMaxPaintFiles;
  }
  storageFileCount_ = count;

  galleryCount_ = 0;
  for (int i = 0; i < count; ++i) {
    if (!names || !names[i] || !*names[i]) {
      continue;
    }
    std::snprintf(galleryNames_[galleryCount_], kPaintFileNameSize, "%s", names[i]);
    ++galleryCount_;
  }

  const int pages = ui_layout::galleryPageCount(galleryCount_);
  if (galleryPage_ >= pages) {
    galleryPage_ = pages - 1;
  }
  if (galleryPage_ < 0) {
    galleryPage_ = 0;
  }
  if (gallerySelected_ >= galleryCount_) {
    gallerySelected_ = -1;
    galleryDialog_ = GalleryDialog::None;
  }
  if (screen_ == Screen::Gallery) {
    dirty_.add(ui_layout::kScreen);
  }
}

void Ui::setDocument(const char* name, bool dirty) {
  std::snprintf(documentName_, sizeof(documentName_), "%s", name ? name : "");
  documentDirty_ = dirty;
}

void Ui::setStorageStats(const StorageStats& stats) {
  storageStats_ = stats;
  if (screen_ == Screen::Settings) {
    dirty_.add(ui_layout::kStorageGaugeArea);
  } else if (screen_ == Screen::Storage) {
    dirty_.add(ui_layout::kScreen);
  } else if (screen_ == Screen::Network) {
    dirty_.add(ui_layout::kNetworkStorageArea);
  }
}

void Ui::setStorageFileCount(int count) {
  storageFileCount_ = count < 0 ? 0 : count;
  if (screen_ == Screen::Storage) {
    dirty_.add(ui_layout::kScreen);
  }
}

void Ui::setNetworkInfo(const NetInfo& info) {
  const bool changed = !sameNetInfo(networkInfo_, info);
  const bool statusChanged = !sameNetworkStatus(networkInfo_, info);
  const bool rssiChanged = networkInfo_.rssi != info.rssi;
  networkInfo_ = info;
  if (!changed) {
    return;
  }
  if (screen_ == Screen::Settings) {
    dirty_.add(ui_layout::kSettingsWifiToggle);
    return;
  }
  if (screen_ != Screen::Network) {
    return;
  }
  if (statusChanged) {
    dirty_.add(ui_layout::kNetworkInfoArea);
    dirty_.add(ui_layout::kNetworkToggleButton);
  } else if (rssiChanged) {
    // Updating a single 320x28 row keeps the once-per-second RSSI refresh
    // cheap; a full information-area repaint would take ~90 ms.
    dirty_.add(ui_layout::kNetworkSignalArea);
  }
}

void Ui::beginOta() {
  if (sheet_ != Sheet::None) {
    setSheet(Sheet::None);
  }
  if (toastActive_) {
    toastActive_ = false;
    dirty_.add(ui_layout::kToast);
  }
  // Drop any button press that was in progress when the upload arrived. The
  // release may only be observed after a failed update, when it must not
  // re-activate the old control.
  pressedButton_ = -1;
  pressedBack_ = false;
  pressedCalibrate_ = false;
  pressedStorage_ = false;
  pressedNetwork_ = false;
  otaActive_ = true;
  otaProgress_ = 0;
  dirty_.add(ui_layout::kScreen);
}

bool Ui::setOtaProgress(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  if (otaProgress_ == percent) {
    return false;
  }
  otaProgress_ = percent;
  if (!otaActive_) {
    return false;
  }
  dirty_.add(ui_layout::kOtaPanel);
  return true;
}

void Ui::endOta() {
  if (!otaActive_) {
    return;
  }
  otaActive_ = false;
  dirty_.add(ui_layout::kOtaPanel);
}

void Ui::promptSaveAs(const char* name) {
  std::snprintf(saveName_, sizeof(saveName_), "%s", name ? name : "");
  setSheet(Sheet::SaveAs);
  dirty_.add(ui_layout::kSheetConfirmNew);
}

const char* Ui::selectedGalleryName() const {
  if (gallerySelected_ < 0 || gallerySelected_ >= galleryCount_) {
    return "";
  }
  return galleryNames_[gallerySelected_];
}

bool Ui::takeSaveRequest(char* out, size_t capacity) {
  if (!saveRequested_) {
    return false;
  }
  saveRequested_ = false;
  if (out && capacity > 0) {
    std::snprintf(out, capacity, "%s", saveName_);
  }
  return true;
}

bool Ui::takeLoadRequest(char* out, size_t capacity) {
  if (!loadRequested_) {
    return false;
  }
  loadRequested_ = false;
  if (out && capacity > 0) {
    std::snprintf(out, capacity, "%s", loadName_);
  }
  return true;
}

bool Ui::takeDeleteRequest(char* out, size_t capacity) {
  if (!deleteRequested_) {
    return false;
  }
  deleteRequested_ = false;
  if (out && capacity > 0) {
    std::snprintf(out, capacity, "%s", deleteName_);
  }
  return true;
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
  pressedStorage_ = false;
  pressedNetwork_ = false;
  pressedRotation_ = false;
  pressedBrightnessDown_ = false;
  pressedBrightnessUp_ = false;
  pressedSettingsWifi_ = false;
  pressedAbout_ = false;
  pressedSettingsNetwork_ = false;
  pressedSafeDisplay_ = false;
  pressedSafeTouch_ = false;
  pressedSafeStorage_ = false;
  pressedSafeContinue_ = false;
  pressedSafeReboot_ = false;
  galleryDialog_ = GalleryDialog::None;
  gallerySelected_ = -1;
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

void Ui::pickColor(uint16_t color, uint32_t nowMs) {
  color_ = color;
  rememberColor(color_);
  dirty_.add(ui_layout::kButtons[2]);
  showToast("Colour picked", nowMs);
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
    case Sheet::Tools: {
      const int option = ui_layout::hitToolOption(p);
      if (option == 0) {
        setSheet(Sheet::Size);
        break;
      }
      if (option == 1 || option == 2) {
        if (!isShapeTool(tool_)) {
          showToast("Shape tools only", nowMs);
          break;
        }
        const bool filled = option == 2;
        if (shapeFilled_ != filled) {
          shapeFilled_ = filled;
          dirty_.add(ui_layout::kToolsOptionsRow);
        }
        break;
      }
      if (item >= 0 && item < 7) {
        tool_ = static_cast<Tool>(item);
        dirty_.add(ui_layout::kButtons[0]);
        setSheet(Sheet::None);
        showToast(kToolNames[item], nowMs);
      }
      break;
    }
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
      if (item >= 0 && item < 7) {
        if (item == 0) {
          setSheet(Sheet::ConfirmNew);
        } else {
          setSheet(Sheet::None);
          switch (item) {
            case 1:
              if (documentName_[0] != '\0') {
                std::snprintf(saveName_, sizeof(saveName_), "%s", documentName_);
                saveRequested_ = true;
              } else {
                saveAsRequested_ = true;
              }
              break;
            case 2:
              saveAsRequested_ = true;
              break;
            case 3:
              backScreen_ = Screen::Canvas;
              setScreen(Screen::Gallery);
              break;
            case 4:
              backScreen_ = Screen::Canvas;
              setScreen(Screen::Settings);
              break;
            case 5:
              backScreen_ = Screen::Canvas;
              setScreen(Screen::Network);
              break;
            case 6:
              backScreen_ = Screen::Canvas;
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
    case Sheet::SaveAs:
      if (ui_layout::kConfirmNewCancel.contains(p)) {
        setSheet(Sheet::None);
      } else if (ui_layout::kConfirmNewOk.contains(p) && saveName_[0] != '\0') {
        setSheet(Sheet::None);
        saveRequested_ = true;
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
        const Rect oldMarker = ui_layout::hsvHueMarkerRect(hsvH_);
        hsvH_ = static_cast<uint8_t>(hue);
        dirty_.add(oldMarker);
        dirty_.add(ui_layout::hsvHueMarkerRect(hsvH_));
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
        const Rect oldMarker = ui_layout::hsvMarkerRect(hsvS_, hsvV_);
        hsvS_ = static_cast<uint8_t>(saturation);
        hsvV_ = static_cast<uint8_t>(value);
        // The square's colours depend only on hue. Repaint just the pixels
        // under the old and new markers, not the whole 216x156 control.
        dirty_.add(oldMarker);
        dirty_.add(ui_layout::hsvMarkerRect(hsvS_, hsvV_));
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

void Ui::updatePlaceholder(const TouchFrame& frame, uint32_t nowMs) {
  const Point p{frame.x, frame.y};

  if (screen_ == Screen::Recovery) {
    updateRecovery(frame);
    return;
  }
  if (screen_ == Screen::SafeMode) {
    updateSafeMode(frame);
    return;
  }

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
        switch (screen_) {
          case Screen::Storage:
            setScreen(safeMode_ ? Screen::SafeMode : backScreen_);
            break;
          case Screen::Network:
          case Screen::About:
            setScreen(backScreen_);
            break;
          default:
            setScreen(Screen::Canvas);
            break;
        }
        return;
      }
    }
  }

  if (screen_ == Screen::Gallery) {
    updateGallery(frame);
    return;
  }

  if (screen_ == Screen::Network) {
    updateNetwork(frame);
    return;
  }

  if (screen_ != Screen::Settings) {
    return;
  }

  if (frame.down) {
    pressedRotation_ = ui_layout::kRotationButton.contains(p);
    pressedBrightnessDown_ = ui_layout::kBrightnessDownButton.contains(p);
    pressedBrightnessUp_ = ui_layout::kBrightnessUpButton.contains(p);
    pressedSettingsWifi_ = ui_layout::kSettingsWifiToggle.contains(p);
    pressedCalibrate_ = ui_layout::kCalibrateButton.contains(p);
    pressedStorage_ = ui_layout::kStorageDetailsButton.contains(p);
    pressedAbout_ = ui_layout::kAboutButton.contains(p);
    pressedSettingsNetwork_ = ui_layout::kSettingsNetworkButton.contains(p);

    if (pressedRotation_) {
      dirty_.add(ui_layout::kRotationButton);
    }
    if (pressedBrightnessDown_) {
      dirty_.add(ui_layout::kBrightnessDownButton);
    }
    if (pressedBrightnessUp_) {
      dirty_.add(ui_layout::kBrightnessUpButton);
    }
    if (pressedSettingsWifi_) {
      dirty_.add(ui_layout::kSettingsWifiToggle);
    }
    if (pressedCalibrate_) {
      dirty_.add(ui_layout::kCalibrateButton);
    }
    if (pressedStorage_) {
      dirty_.add(ui_layout::kStorageDetailsButton);
    }
    if (pressedAbout_) {
      dirty_.add(ui_layout::kAboutButton);
    }
    if (pressedSettingsNetwork_) {
      dirty_.add(ui_layout::kSettingsNetworkButton);
    }
  }

  if (frame.move) {
    if (pressedRotation_ && !ui_layout::kRotationButton.contains(p)) {
      pressedRotation_ = false;
      dirty_.add(ui_layout::kRotationButton);
    }
    if (pressedBrightnessDown_ &&
        !ui_layout::kBrightnessDownButton.contains(p)) {
      pressedBrightnessDown_ = false;
      dirty_.add(ui_layout::kBrightnessDownButton);
    }
    if (pressedBrightnessUp_ &&
        !ui_layout::kBrightnessUpButton.contains(p)) {
      pressedBrightnessUp_ = false;
      dirty_.add(ui_layout::kBrightnessUpButton);
    }
    if (pressedSettingsWifi_ &&
        !ui_layout::kSettingsWifiToggle.contains(p)) {
      pressedSettingsWifi_ = false;
      dirty_.add(ui_layout::kSettingsWifiToggle);
    }
    if (pressedCalibrate_ && !ui_layout::kCalibrateButton.contains(p)) {
      pressedCalibrate_ = false;
      dirty_.add(ui_layout::kCalibrateButton);
    }
    if (pressedStorage_ && !ui_layout::kStorageDetailsButton.contains(p)) {
      pressedStorage_ = false;
      dirty_.add(ui_layout::kStorageDetailsButton);
    }
    if (pressedAbout_ && !ui_layout::kAboutButton.contains(p)) {
      pressedAbout_ = false;
      dirty_.add(ui_layout::kAboutButton);
    }
    if (pressedSettingsNetwork_ &&
        !ui_layout::kSettingsNetworkButton.contains(p)) {
      pressedSettingsNetwork_ = false;
      dirty_.add(ui_layout::kSettingsNetworkButton);
    }
  }

  if (frame.up) {
    const bool wasRotation = pressedRotation_;
    const bool wasBrightnessDown = pressedBrightnessDown_;
    const bool wasBrightnessUp = pressedBrightnessUp_;
    const bool wasWifi = pressedSettingsWifi_;
    const bool wasCalibrate = pressedCalibrate_;
    const bool wasStorage = pressedStorage_;
    const bool wasAbout = pressedAbout_;
    const bool wasNetwork = pressedSettingsNetwork_;

    pressedRotation_ = false;
    pressedBrightnessDown_ = false;
    pressedBrightnessUp_ = false;
    pressedSettingsWifi_ = false;
    pressedCalibrate_ = false;
    pressedStorage_ = false;
    pressedAbout_ = false;
    pressedSettingsNetwork_ = false;

    if (wasRotation) {
      dirty_.add(ui_layout::kRotationButton);
      if (ui_layout::kRotationButton.contains(p)) {
        rotationToggleRequested_ = true;
      }
    }
    if (wasBrightnessDown) {
      dirty_.add(ui_layout::kBrightnessDownButton);
      if (ui_layout::kBrightnessDownButton.contains(p) &&
          brightness_ > config::kBrightnessLevels[0]) {
        brightnessStep_ = -1;
        brightnessStepRequested_ = true;
      }
    }
    if (wasBrightnessUp) {
      dirty_.add(ui_layout::kBrightnessUpButton);
      if (ui_layout::kBrightnessUpButton.contains(p) &&
          brightness_ <
              config::kBrightnessLevels[config::kBrightnessLevelCount - 1]) {
        brightnessStep_ = 1;
        brightnessStepRequested_ = true;
      }
    }
    if (wasWifi) {
      dirty_.add(ui_layout::kSettingsWifiToggle);
      if (ui_layout::kSettingsWifiToggle.contains(p)) {
        if (networkCanToggle()) {
          networkToggleRequested_ = true;
        } else if (!networkInfo_.configured) {
          showToast("Wi-Fi not configured", nowMs);
        } else if (networkInfo_.lowStorage) {
          showToast("Need 700 KB free for OTA", nowMs);
        }
      }
    }
    if (wasCalibrate) {
      dirty_.add(ui_layout::kCalibrateButton);
      if (ui_layout::kCalibrateButton.contains(p)) {
        startCalibration();
      }
    }
    if (wasStorage) {
      dirty_.add(ui_layout::kStorageDetailsButton);
      if (ui_layout::kStorageDetailsButton.contains(p)) {
        backScreen_ = Screen::Settings;
        setScreen(Screen::Storage);
      }
    }
    if (wasAbout) {
      dirty_.add(ui_layout::kAboutButton);
      if (ui_layout::kAboutButton.contains(p)) {
        backScreen_ = Screen::Settings;
        setScreen(Screen::About);
      }
    }
    if (wasNetwork) {
      dirty_.add(ui_layout::kSettingsNetworkButton);
      if (ui_layout::kSettingsNetworkButton.contains(p)) {
        backScreen_ = Screen::Settings;
        setScreen(Screen::Network);
      }
    }
  }
}

void Ui::setGalleryDialog(GalleryDialog next) {
  if (galleryDialog_ == next) {
    return;
  }
  galleryDialog_ = next;
  dirty_.add(ui_layout::kScreen);
}

void Ui::updateGallery(const TouchFrame& frame) {
  if (galleryDialog_ != GalleryDialog::None) {
    if (!frame.tap) {
      return;
    }
    const Point p{frame.x, frame.y};
    if (ui_layout::kGalleryCancel.contains(p)) {
      setGalleryDialog(GalleryDialog::None);
    } else if (galleryDialog_ == GalleryDialog::Load &&
               ui_layout::kGalleryOpen.contains(p)) {
      std::snprintf(loadName_, sizeof(loadName_), "%s", selectedGalleryName());
      loadRequested_ = true;
      setGalleryDialog(GalleryDialog::None);
    } else if (galleryDialog_ == GalleryDialog::Delete &&
               ui_layout::kGalleryDelete.contains(p)) {
      std::snprintf(deleteName_, sizeof(deleteName_), "%s",
                    selectedGalleryName());
      deleteRequested_ = true;
      setGalleryDialog(GalleryDialog::None);
    }
    return;
  }

  const Point p{frame.x, frame.y};

  // A long press selects a file for deletion; a normal tap opens the load
  // confirmation. Keeping deletion behind a long press prevents accidental
  // data loss from a single tap.
  if (frame.longPress) {
    const int slot = ui_layout::hitGalleryItem(p);
    const int index = galleryPage_ * ui_layout::kGalleryPerPage + slot;
    if (slot >= 0 && index >= 0 && index < galleryCount_) {
      gallerySelected_ = index;
      setGalleryDialog(GalleryDialog::Delete);
    }
    return;
  }

  if (!frame.tap) {
    return;
  }

  if (ui_layout::kGalleryPrev.contains(p)) {
    if (galleryPage_ > 0) {
      --galleryPage_;
      dirty_.add(ui_layout::kScreen);
    }
    return;
  }

  if (ui_layout::kGalleryNext.contains(p)) {
    if (galleryPage_ + 1 < ui_layout::galleryPageCount(galleryCount_)) {
      ++galleryPage_;
      dirty_.add(ui_layout::kScreen);
    }
    return;
  }

  const int slot = ui_layout::hitGalleryItem(p);
  const int index = galleryPage_ * ui_layout::kGalleryPerPage + slot;
  if (slot >= 0 && index >= 0 && index < galleryCount_) {
    gallerySelected_ = index;
    setGalleryDialog(GalleryDialog::Load);
  }
}

bool Ui::networkCanToggle() const {
  // The toggle is meaningful only with a configured network. Enabling also
  // needs enough LittleFS space for an OTA staging image; disabling an already
  // enabled link must remain possible even when storage is low. This also keeps
  // an inconsistent "enabled but unconfigured" state from accepting a tap.
  return networkInfo_.configured &&
         (networkInfo_.enabled || !networkInfo_.lowStorage);
}

void Ui::updateNetwork(const TouchFrame& frame) {
  const Point p{frame.x, frame.y};
  const bool canToggle = networkCanToggle();

  if (frame.down) {
    pressedNetwork_ = canToggle && ui_layout::kNetworkToggleButton.contains(p);
    dirty_.add(ui_layout::kNetworkToggleButton);
  }

  if (frame.move && pressedNetwork_ &&
      !ui_layout::kNetworkToggleButton.contains(p)) {
    pressedNetwork_ = false;
    dirty_.add(ui_layout::kNetworkToggleButton);
  }

  if (frame.up) {
    const bool wasPressed = pressedNetwork_;
    pressedNetwork_ = false;
    dirty_.add(ui_layout::kNetworkToggleButton);
    if (wasPressed && canToggle &&
        ui_layout::kNetworkToggleButton.contains(p)) {
      networkToggleRequested_ = true;
    }
  }
}

void Ui::updateRecovery(const TouchFrame& frame) {
  if (!frame.tap || recoveryRestoreRequested_ || recoveryDiscardRequested_) {
    return;
  }
  const Point p{frame.x, frame.y};
  if (ui_layout::kRecoveryRestore.contains(p)) {
    recoveryAvailable_ = false;
    recoveryRestoreRequested_ = true;
    dirty_.add(ui_layout::kScreen);
  } else if (ui_layout::kRecoveryDiscard.contains(p)) {
    recoveryAvailable_ = false;
    recoveryDiscardRequested_ = true;
    dirty_.add(ui_layout::kScreen);
  }
}

void Ui::updateSafeMode(const TouchFrame& frame) {
  const Point p{frame.x, frame.y};

  if (frame.down) {
    pressedSafeDisplay_ = ui_layout::kSafeDisplayTest.contains(p);
    pressedSafeTouch_ = ui_layout::kSafeTouchTest.contains(p);
    pressedSafeStorage_ = ui_layout::kSafeStorageTest.contains(p);
    pressedSafeContinue_ = ui_layout::kSafeContinue.contains(p);
    pressedSafeReboot_ = ui_layout::kSafeReboot.contains(p);
  }

  if (frame.move) {
    if (pressedSafeDisplay_ && !ui_layout::kSafeDisplayTest.contains(p)) {
      pressedSafeDisplay_ = false;
    }
    if (pressedSafeTouch_ && !ui_layout::kSafeTouchTest.contains(p)) {
      pressedSafeTouch_ = false;
    }
    if (pressedSafeStorage_ && !ui_layout::kSafeStorageTest.contains(p)) {
      pressedSafeStorage_ = false;
    }
    if (pressedSafeContinue_ && !ui_layout::kSafeContinue.contains(p)) {
      pressedSafeContinue_ = false;
    }
    if (pressedSafeReboot_ && !ui_layout::kSafeReboot.contains(p)) {
      pressedSafeReboot_ = false;
    }
  }

  if (!frame.up) {
    return;
  }

  const bool wasDisplay = pressedSafeDisplay_;
  const bool wasTouch = pressedSafeTouch_;
  const bool wasStorage = pressedSafeStorage_;
  const bool wasContinue = pressedSafeContinue_;
  const bool wasReboot = pressedSafeReboot_;
  pressedSafeDisplay_ = false;
  pressedSafeTouch_ = false;
  pressedSafeStorage_ = false;
  pressedSafeContinue_ = false;
  pressedSafeReboot_ = false;

  if (wasDisplay && ui_layout::kSafeDisplayTest.contains(p)) {
    setScreen(Screen::DisplayTest);
  } else if (wasTouch && ui_layout::kSafeTouchTest.contains(p)) {
    touchTestSeen_ = false;
    touchTestX_ = config::kScreenWidth / 2;
    touchTestY_ = config::kScreenHeight / 2;
    touchTestRawX_ = 0;
    touchTestRawY_ = 0;
    setScreen(Screen::TouchTest);
  } else if (wasStorage && ui_layout::kSafeStorageTest.contains(p)) {
    setScreen(Screen::Storage);
  } else if (wasContinue && ui_layout::kSafeContinue.contains(p)) {
    safeContinueRequested_ = true;
    dirty_.add(ui_layout::kScreen);
  } else if (wasReboot && ui_layout::kSafeReboot.contains(p)) {
    safeRebootRequested_ = true;
    dirty_.add(ui_layout::kScreen);
  }
}

void Ui::updateTouchTest(const TouchFrame& frame) {
  if (frame.pressed || frame.down) {
    touchTestSeen_ = true;
    touchTestX_ = frame.x;
    touchTestY_ = frame.y;
    touchTestRawX_ = frame.rawX;
    touchTestRawY_ = frame.rawY;
    dirty_.add(ui_layout::kTouchTestArea);
  }

  if (frame.up && ui_layout::kTouchTestBack.contains({frame.x, frame.y})) {
    setScreen(Screen::SafeMode);
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

  // Calibration is always stored in the canonical rotation-2 space. If the
  // display is currently flipped, the crosshair's framebuffer coordinate is
  // flipped back before fitting.
  const Point canonicalTarget =
      mapDisplayToCanonical(target, displayRotation_);
  calibrationPoints_[calibrationStep_] = {
      static_cast<int32_t>(frame.rawX), static_cast<int32_t>(frame.rawY),
      canonicalTarget.x, canonicalTarget.y};
  ++calibrationStep_;

  if (calibrationStep_ == 4) {
    calibrationResult_ = makeFourPointCalibration(calibrationPoints_);
    if (isCalibrationPlausible(calibrationResult_) &&
        calibrationMatchesPoints(calibrationResult_, calibrationPoints_)) {
      calibrationComplete_ = true;
    } else {
      calibrationStep_ = 0;
      showToast("Calibration failed", nowMs);
    }
  }
  dirty_.add(ui_layout::kScreen);
}

void Ui::calibrationFinished(bool saved, uint32_t nowMs) {
  touchCalibrated_ = true;
  setScreen(Screen::Settings);
  showToast(saved ? "Calibration saved" : "Calibration unsaved", nowMs);
}

void Ui::update(const TouchFrame& frame, uint32_t nowMs) {
  if (screen_ == Screen::Splash) {
    // A tap no longer skips the splash; holding the touch shows intent. The
    // timeout still guarantees boot even if the panel is noisy.
    if (frame.longPress || static_cast<int32_t>(nowMs - splashUntil_) >= 0) {
      if (safeMode_) {
        screen_ = Screen::SafeMode;
      } else {
        screen_ = recoveryAvailable_ ? Screen::Recovery : Screen::Canvas;
      }
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

  if (screen_ == Screen::TouchTest) {
    updateTouchTest(frame);
    return;
  }
  if (screen_ == Screen::DisplayTest) {
    if (frame.down && ui_layout::kDisplayTestBack.contains({frame.x, frame.y})) {
      setScreen(Screen::SafeMode);
    }
    return;
  }

  if (screen_ != Screen::Canvas) {
    updatePlaceholder(frame, nowMs);
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
    case Screen::Storage:
    case Screen::Network:
    case Screen::About:
    case Screen::Recovery:
      drawPlaceholder(fb, fbWidth, clip);
      break;
    case Screen::Calibrate:
      drawCalibration(fb, fbWidth, clip);
      break;
    case Screen::SafeMode:
      drawSafeMode(fb, fbWidth, clip);
      break;
    case Screen::DisplayTest:
      drawDisplayTest(fb, fbWidth, clip);
      break;
    case Screen::TouchTest:
      drawTouchTest(fb, fbWidth, clip);
      break;
  }

  if (toastActive_ && screen_ != Screen::Splash) {
    drawToast(fb, fbWidth, clip);
  }
  if (otaActive_) {
    drawOtaOverlay(fb, fbWidth, clip);
  }
}

void Ui::drawSplash(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kSplashBg);

  // Simple painted-palette mark: a ring with three colour dots and a brush.
  const Point mark{160, 94};
  gfx::drawCircle(fb, fbWidth, clip, mark, 40, kAccent);
  gfx::drawCircle(fb, fbWidth, clip, mark, 39, kChromeLine);
  gfx::fillCircle(fb, fbWidth, clip, {133, 82}, 7, kRed);
  gfx::fillCircle(fb, fbWidth, clip, {160, 73}, 7, kGreen);
  gfx::fillCircle(fb, fbWidth, clip, {187, 82}, 7, kBlue);
  drawToolIcon(fb, fbWidth, clip, {136, 92, 48, 48}, Tool::Brush, kWhite);

  drawTextCentered(fb, fbWidth, clip, {0, 150, 320, 90}, "Pico Paint", kWhite,
                   4);
  drawTextCentered(fb, fbWidth, clip, {0, 228, 320, 30},
                   "Draw. Save. Update.", kAccent, 2);

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "v%s", PAINT_VERSION);
  drawTextCentered(fb, fbWidth, clip, {0, 264, 320, 24}, buffer, kTextDim, 2);
  drawTextCentered(fb, fbWidth, clip, {0, 304, 320, 24},
                   "Hold to skip", kTextDim, 2);

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
    case Sheet::Tools: {
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 8)},
                    "Tools", kText, 2);
      for (int i = 0; i < 7; ++i) {
        const Rect r = ui_layout::toolItemRect(i);
        const bool active = tool_ == static_cast<Tool>(i);
        gfx::fillRect(fb, fbWidth, clip, r, active ? kSheetItemActive : kSheetItem);
        gfx::drawRect(fb, fbWidth, clip, r, active ? kAccent : kButtonBorder);
        drawTextLeftCentered(fb, fbWidth, clip, r, kToolNames[i], kText, 2, 8);
      }

      // Contextual options row: brush/size entry point plus the shape fill
      // mode. Outline/filled are disabled while a non-shape tool is active.
      const Rect size = ui_layout::kToolsSizeButton;
      gfx::fillRect(fb, fbWidth, clip, size, kButton);
      gfx::drawRect(fb, fbWidth, clip, size, kButtonBorder);
      char sizeLabel[16];
      std::snprintf(sizeLabel, sizeof(sizeLabel), "Size %u",
                    static_cast<unsigned>(brushSize_));
      drawTextCentered(fb, fbWidth, clip, size, sizeLabel, kText, 2);

      const bool shapes = isShapeTool(tool_);
      const struct {
        Rect rect;
        const char* label;
        bool active;
      } options[2] = {
          {ui_layout::kToolsOutlineButton, "Outline", shapes && !shapeFilled_},
          {ui_layout::kToolsFilledButton, "Filled", shapes && shapeFilled_},
      };
      for (const auto& option : options) {
        const uint16_t fill = option.active ? kSheetItemActive : kButton;
        const uint16_t border =
            option.active ? kAccent : (shapes ? kButtonBorder : kDisabled);
        gfx::fillRect(fb, fbWidth, clip, option.rect, fill);
        gfx::drawRect(fb, fbWidth, clip, option.rect, border);
        drawTextCentered(fb, fbWidth, clip, option.rect, option.label,
                         shapes ? kText : kDisabled, 2);
      }
      break;
    }

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
      for (int i = 0; i < 7; ++i) {
        const Rect r = ui_layout::menuItemRect(i);
        gfx::fillRect(fb, fbWidth, clip, r, kSheetItem);
        gfx::drawRect(fb, fbWidth, clip, r, kButtonBorder);
        drawTextLeftCentered(fb, fbWidth, clip, r, kMenuNames[i], kText, 2, 10);
      }
      break;

    case Sheet::SaveAs:
      gfx::drawText(fb, fbWidth, clip, {16, static_cast<int16_t>(sheet.y + 12)},
                    "Save as", kText, 2);
      gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(sheet.y + 54)},
                    "Create a new gallery file:", kTextDim, 2);
      drawTextCentered(fb, fbWidth, clip,
                       {20, static_cast<int16_t>(sheet.y + 82), 280, 24},
                       saveName_[0] ? saveName_ : "(no name)", kWhite, 2);
      gfx::drawText(fb, fbWidth, clip, {20, static_cast<int16_t>(sheet.y + 116)},
                    "The current drawing stays open.", kTextDim, 2);
      gfx::fillRect(fb, fbWidth, clip, ui_layout::kConfirmNewCancel, kButton);
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kConfirmNewCancel, kButtonBorder);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kConfirmNewCancel, "Cancel",
                       kText, 2);
      gfx::fillRect(fb, fbWidth, clip, ui_layout::kConfirmNewOk, kButtonActive);
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kConfirmNewOk, kAccent);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kConfirmNewOk, "Save",
                       kText, 2);
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

  if (screen_ != Screen::Recovery) {
    drawButtonFrame(fb, fbWidth, clip, ui_layout::kBackButton, pressedBack_,
                    false, false);
    drawArrowIcon(fb, fbWidth, clip, ui_layout::kBackButton, false, kText);
  }

  const char* title = "Gallery";
  switch (screen_) {
    case Screen::Gallery:
      title = "Gallery";
      break;
    case Screen::Settings:
      title = "Settings";
      break;
    case Screen::Storage:
      title = "Storage";
      break;
    case Screen::Network:
      title = "Wi-Fi & OTA";
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
  if (screen_ == Screen::Recovery) {
    drawTextCentered(fb, fbWidth, clip, {0, 0, 320, config::kTopBarHeight},
                     title, kText, 3);
  } else {
    gfx::drawText(fb, fbWidth, clip, {80, 12}, title, kText, 3);
  }

  switch (screen_) {
    case Screen::Gallery:
      drawGallery(fb, fbWidth, clip);
      drawGalleryDialog(fb, fbWidth, clip);
      break;

    case Screen::Settings: {
      char buffer[48];

      // Rotation: both supported values are portrait; the whole row is one
      // large target.
      drawButtonFrame(fb, fbWidth, clip, ui_layout::kRotationButton,
                      pressedRotation_, false, false);
      drawTextLeftCentered(fb, fbWidth, clip, ui_layout::kRotationButton,
                           "Rotation", kText, 2, 10);
      drawTextRightCentered(fb, fbWidth, clip, ui_layout::kRotationButton,
                            displayRotation_ == 0 ? "Flipped" : "Upright",
                            kAccent, 2, 10);

      // Brightness: minus / percentage / plus. The value is text as well as a
      // visual level so the setting is not colour-only.
      gfx::fillRect(fb, fbWidth, clip, ui_layout::kBrightnessRow, kButton);
      gfx::drawRect(fb, fbWidth, clip, ui_layout::kBrightnessRow,
                    kButtonBorder);
      drawButtonFrame(fb, fbWidth, clip, ui_layout::kBrightnessDownButton,
                      pressedBrightnessDown_, false,
                      brightness_ <= config::kBrightnessLevels[0]);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kBrightnessDownButton,
                       "-", kText, 3);
      drawButtonFrame(fb, fbWidth, clip, ui_layout::kBrightnessUpButton,
                      pressedBrightnessUp_, false,
                      brightness_ >= config::kBrightnessLevels[
                                        config::kBrightnessLevelCount - 1]);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kBrightnessUpButton, "+",
                       kText, 3);
      std::snprintf(buffer, sizeof(buffer), "Brightness %u%%",
                    static_cast<unsigned>((brightness_ * 100u) / 255u));
      drawTextCentered(fb, fbWidth, clip,
                       {72, ui_layout::kBrightnessRow.y, 176,
                        ui_layout::kBrightnessRow.h},
                       buffer, kText, 2);

      // Wi-Fi / OTA toggle. The label carries the state; the border and fill
      // provide a second cue.
      const bool wifiOn = networkInfo_.enabled;
      const bool wifiCanToggle = networkCanToggle();
      drawButtonFrame(fb, fbWidth, clip, ui_layout::kSettingsWifiToggle,
                      pressedSettingsWifi_, wifiOn, !wifiCanToggle);
      drawTextLeftCentered(fb, fbWidth, clip,
                           ui_layout::kSettingsWifiToggle, "Wi-Fi & OTA",
                           wifiOn ? kText : kTextDim, 2, 10);
      drawTextRightCentered(fb, fbWidth, clip,
                            ui_layout::kSettingsWifiToggle,
                            wifiOn ? "On" : "Off",
                            wifiOn ? kAccent : kTextDim, 2, 10);

      drawButtonFrame(fb, fbWidth, clip, ui_layout::kCalibrateButton,
                      pressedCalibrate_, false, false);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kCalibrateButton,
                       "Calibrate", kText, 2);

      drawButtonFrame(fb, fbWidth, clip, ui_layout::kStorageDetailsButton,
                      pressedStorage_, false, false);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kStorageDetailsButton,
                       "Storage", kText, 2);

      drawButtonFrame(fb, fbWidth, clip, ui_layout::kAboutButton, pressedAbout_,
                      false, false);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kAboutButton, "About",
                       kText, 2);

      drawButtonFrame(fb, fbWidth, clip, ui_layout::kSettingsNetworkButton,
                      pressedSettingsNetwork_, false, false);
      drawTextCentered(fb, fbWidth, clip, ui_layout::kSettingsNetworkButton,
                       "Wi-Fi info", kText, 2);

      // Compact storage gauge. Reserve status is always written out, never
      // encoded only as a colour.
      if (storageStats_.totalBytes == 0) {
        gfx::drawText(fb, fbWidth, clip, {20, 328}, "Storage unavailable",
                      kDanger, 2);
      } else {
        std::snprintf(buffer, sizeof(buffer), "Storage: %u KB free",
                      static_cast<unsigned>(storageStats_.freeBytes / 1024u));
        gfx::drawText(fb, fbWidth, clip, {20, 328}, buffer, kText, 2);

        const Rect bar = ui_layout::kStorageBar;
        gfx::fillRect(fb, fbWidth, clip, bar, kSheetItem);
        const int usedWidth =
            (bar.w * static_cast<int>(storageStats_.usedPercent())) / 100;
        if (usedWidth > 0) {
          gfx::fillRect(fb, fbWidth, clip,
                        {bar.x, bar.y, static_cast<int16_t>(usedWidth), bar.h},
                        storageStats_.otaHeadroom() ? kAccent : kDanger);
        }
        gfx::drawRect(fb, fbWidth, clip, bar, kButtonBorder);

        std::snprintf(buffer, sizeof(buffer), "Used %u%% of %u KB",
                      static_cast<unsigned>(storageStats_.usedPercent()),
                      static_cast<unsigned>(storageStats_.totalBytes / 1024u));
        gfx::drawText(fb, fbWidth, clip, {20, 384}, buffer, kTextDim, 2);
        gfx::drawText(fb, fbWidth, clip, {20, 408},
                      storageStats_.otaHeadroom() ? "OTA reserve: OK"
                                                  : "OTA reserve: LOW",
                      storageStats_.otaHeadroom() ? kText : kDanger, 2);
      }

      std::snprintf(buffer, sizeof(buffer), "v%s", PAINT_VERSION);
      drawTextCentered(fb, fbWidth, clip, {0, 446, 320, 20}, buffer, kTextDim,
                       2);
      break;
    }

    case Screen::Storage: {
      char buffer[48];
      const bool mounted = storageStats_.totalBytes != 0;
      gfx::drawText(fb, fbWidth, clip, {20, 84},
                    mounted ? "Mount: mounted (LittleFS)"
                            : "Mount: unavailable",
                    mounted ? kText : kDanger, 2);
      if (!mounted) {
        gfx::drawText(fb, fbWidth, clip, {20, 120},
                      "Filesystem not mounted", kTextDim, 2);
        gfx::drawText(fb, fbWidth, clip, {20, 152},
                      "Saving and OTA disabled", kTextDim, 2);
        break;
      }

      std::snprintf(buffer, sizeof(buffer), "Total: %u KB",
                    static_cast<unsigned>(storageStats_.totalBytes / 1024u));
      gfx::drawText(fb, fbWidth, clip, {20, 116}, buffer, kText, 2);
      std::snprintf(buffer, sizeof(buffer), "Used: %u KB (%u%%)",
                    static_cast<unsigned>(storageStats_.usedBytes / 1024u),
                    static_cast<unsigned>(storageStats_.usedPercent()));
      gfx::drawText(fb, fbWidth, clip, {20, 148}, buffer, kText, 2);
      std::snprintf(buffer, sizeof(buffer), "Free: %u KB",
                    static_cast<unsigned>(storageStats_.freeBytes / 1024u));
      gfx::drawText(fb, fbWidth, clip, {20, 180}, buffer, kText, 2);
      std::snprintf(buffer, sizeof(buffer), "Pictures: %d", storageFileCount_);
      gfx::drawText(fb, fbWidth, clip, {20, 212}, buffer, kText, 2);
      gfx::drawText(fb, fbWidth, clip, {20, 244},
                    storageStats_.otaHeadroom() ? "OTA reserve: OK"
                                                : "OTA reserve: LOW",
                    storageStats_.otaHeadroom() ? kAccent : kDanger, 2);
      std::snprintf(buffer, sizeof(buffer), "Reserve: %u KB",
                    static_cast<unsigned>(kOtaHeadroomBytes / 1024u));
      gfx::drawText(fb, fbWidth, clip, {20, 276}, buffer, kTextDim, 2);
      break;
    }

    case Screen::Network:
      drawNetwork(fb, fbWidth, clip);
      break;

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
      gfx::drawText(fb, fbWidth, clip, {20, 340}, "M8 settings + recovery",
                    kTextDim, 2);
      break;
    }

    case Screen::Recovery:
      drawRecovery(fb, fbWidth, clip);
      break;

    default:
      break;
  }
}

void Ui::drawNetwork(uint16_t* fb, int fbWidth, const Rect& clip) {
  const NetInfo& info = networkInfo_;
  char buffer[48];

  const char* status = "Wi-Fi: off";
  uint16_t statusColor = kTextDim;
  if (!info.configured) {
    status = "Wi-Fi: not configured";
    statusColor = kDanger;
  } else {
    switch (info.state) {
      case NetState::Disabled:
        status = "Wi-Fi: off";
        break;
      case NetState::Offline:
        status = info.lowStorage ? "Wi-Fi: paused (low storage)"
                                 : "Wi-Fi: offline";
        statusColor = kTextDim;
        break;
      case NetState::Connecting:
        status = "Wi-Fi: connecting";
        statusColor = kText;
        break;
      case NetState::Connected:
        status = "Wi-Fi: connected";
        statusColor = kAccent;
        break;
      case NetState::OtaActive:
        status = "Wi-Fi: OTA active";
        statusColor = kAccent;
        break;
    }
  }
  gfx::drawText(fb, fbWidth, clip, {20, 84}, status, statusColor, 2);

  gfx::drawText(fb, fbWidth, clip, {20, 112},
                info.configured ? "Credentials: configured"
                                : "Credentials: missing",
                info.configured ? kText : kDanger, 2);

  if (info.hasIp()) {
    std::snprintf(buffer, sizeof(buffer), "IP: %u.%u.%u.%u",
                  static_cast<unsigned>(info.ip[0]),
                  static_cast<unsigned>(info.ip[1]),
                  static_cast<unsigned>(info.ip[2]),
                  static_cast<unsigned>(info.ip[3]));
  } else {
    std::snprintf(buffer, sizeof(buffer), "IP: --");
  }
  gfx::drawText(fb, fbWidth, clip, {20, 140}, buffer,
                info.hasIp() ? kText : kTextDim, 2);

  std::snprintf(buffer, sizeof(buffer), "Host: %s:%u", config::kOtaHostname,
                static_cast<unsigned>(config::kOtaPort));
  gfx::drawText(fb, fbWidth, clip, {20, 168}, buffer, kText, 2);

  if (info.state == NetState::Connected ||
      info.state == NetState::OtaActive) {
    std::snprintf(buffer, sizeof(buffer), "Signal: %ld dBm",
                  static_cast<long>(info.rssi));
    gfx::drawText(fb, fbWidth, clip, {20, 196}, buffer, kText, 2);
  } else {
    gfx::drawText(fb, fbWidth, clip, {20, 196}, "Signal: --", kTextDim, 2);
  }

  const char* server = "OTA server: off";
  if (info.server) {
    server = "OTA server: listening";
  } else if (info.lowStorage) {
    server = "OTA server: paused";
  }
  gfx::drawText(fb, fbWidth, clip, {20, 224}, server,
                info.server ? kAccent : kTextDim, 2);

  const bool storageKnown = storageStats_.totalBytes != 0;
  const bool headroom = storageKnown && storageStats_.otaHeadroom();
  gfx::drawText(fb, fbWidth, clip, {20, 252},
                storageKnown
                    ? (headroom ? "OTA reserve: OK" : "OTA reserve: LOW")
                    : "OTA reserve: unknown",
                headroom ? kAccent : kDanger, 2);

  if (info.error != OtaError::None) {
    const char* error = "OTA error";
    switch (info.error) {
      case OtaError::Auth:
        error = "Last OTA: auth failed";
        break;
      case OtaError::Begin:
        error = "Last OTA: begin failed";
        break;
      case OtaError::Connect:
        error = "Last OTA: connect failed";
        break;
      case OtaError::Receive:
        error = "Last OTA: receive failed";
        break;
      case OtaError::End:
        error = "Last OTA: end failed";
        break;
      default:
        break;
    }
    gfx::drawText(fb, fbWidth, clip, {20, 284}, error, kDanger, 2);
  }

  const bool canToggle = networkCanToggle();
  const bool pressed = pressedNetwork_ && canToggle;
  drawButtonFrame(fb, fbWidth, clip, ui_layout::kNetworkToggleButton, pressed,
                  false, !canToggle);

  char label[32];
  if (!info.configured) {
    std::snprintf(label, sizeof(label), "Wi-Fi not configured");
  } else if (!info.enabled) {
    std::snprintf(label, sizeof(label), "Enable Wi-Fi & OTA");
  } else {
    std::snprintf(label, sizeof(label), "Disable Wi-Fi & OTA");
  }
  drawTextCentered(fb, fbWidth, clip, ui_layout::kNetworkToggleButton, label,
                   canToggle ? kText : kDisabled, 2);
}

void Ui::drawOtaOverlay(uint16_t* fb, int fbWidth, const Rect& clip) {
  const Rect panel = ui_layout::kOtaPanel;
  gfx::fillRect(fb, fbWidth, clip, panel, kToastBg);
  gfx::drawRect(fb, fbWidth, clip, panel, kAccent);
  gfx::fillRect(fb, fbWidth, clip, {panel.x, panel.y, panel.w, 3}, kAccent);

  drawTextCentered(fb, fbWidth, clip,
                   {panel.x, static_cast<int16_t>(panel.y + 14), panel.w, 24},
                   "Firmware update", kWhite, 2);
  drawTextCentered(fb, fbWidth, clip,
                   {panel.x, static_cast<int16_t>(panel.y + 42), panel.w, 20},
                   "Do not power off", kTextDim, 2);

  const Rect bar = ui_layout::kOtaProgressBar;
  gfx::fillRect(fb, fbWidth, clip, bar, kSheetItem);
  int filled = (bar.w * static_cast<int>(otaProgress_)) / 100;
  if (filled > 0) {
    gfx::fillRect(fb, fbWidth, clip,
                  {bar.x, bar.y, static_cast<int16_t>(filled), bar.h}, kAccent);
  }
  gfx::drawRect(fb, fbWidth, clip, bar, kButtonBorder);

  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%u%%",
                static_cast<unsigned>(otaProgress_));
  drawTextCentered(fb, fbWidth, clip,
                   {panel.x, static_cast<int16_t>(panel.y + 108), panel.w, 24},
                   buffer, kText, 2);
}

void Ui::drawGallery(uint16_t* fb, int fbWidth, const Rect& clip) {
  if (galleryCount_ == 0) {
    drawTextCentered(fb, fbWidth, clip, {0, 170, 320, 32}, "No drawings yet",
                     kTextDim, 3);
    drawTextCentered(fb, fbWidth, clip, {0, 218, 320, 24},
                     "Save a drawing to see it here", kTextDim, 2);
    return;
  }

  for (int slot = 0; slot < ui_layout::kGalleryPerPage; ++slot) {
    const int index = galleryPage_ * ui_layout::kGalleryPerPage + slot;
    if (index >= galleryCount_) {
      break;
    }

    const Rect r = ui_layout::kGalleryItems[slot];
    const bool selected =
        galleryDialog_ != GalleryDialog::None && gallerySelected_ == index;
    gfx::fillRect(fb, fbWidth, clip, r, kButton);
    gfx::drawRect(fb, fbWidth, clip, r, selected ? kAccent : kButtonBorder);

    const Rect thumbnail{static_cast<int16_t>(r.x + 4),
                         static_cast<int16_t>(r.y + 4),
                         static_cast<int16_t>(r.w - 8),
                         static_cast<int16_t>(r.h - 26)};
    const bool drew = thumbnailRenderer_ &&
                      thumbnailRenderer_(thumbnailContext_, fb, fbWidth, clip,
                                         thumbnail, galleryNames_[index]);
    if (!drew) {
      gfx::fillRect(fb, fbWidth, clip, thumbnail, kSheetItem);
      drawTextCentered(fb, fbWidth, clip, thumbnail, "Preview", kTextDim, 2);
    }

    char label[16];
    std::snprintf(label, sizeof(label), "%s", galleryNames_[index]);
    char* dot = std::strrchr(label, '.');
    if (dot && dot != label) {
      *dot = '\0';
    }
    if (std::strlen(label) > 12) {
      label[12] = '\0';
    }
    drawTextCentered(fb, fbWidth, clip,
                     {r.x, static_cast<int16_t>(r.y + r.h - 20), r.w, 20},
                     label, kText, 2);
  }

  const int pages = ui_layout::galleryPageCount(galleryCount_);
  const bool canPrev = galleryPage_ > 0;
  const bool canNext = galleryPage_ + 1 < pages;

  drawButtonFrame(fb, fbWidth, clip, ui_layout::kGalleryPrev, false, false,
                  !canPrev);
  drawArrowIcon(fb, fbWidth, clip, ui_layout::kGalleryPrev, false,
                canPrev ? kText : kDisabled);
  drawButtonFrame(fb, fbWidth, clip, ui_layout::kGalleryNext, false, false,
                  !canNext);
  drawArrowIcon(fb, fbWidth, clip, ui_layout::kGalleryNext, true,
                canNext ? kText : kDisabled);

  char page[20];
  std::snprintf(page, sizeof(page), "%d / %d", galleryPage_ + 1, pages);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kGalleryPageLabel, page,
                   kTextDim, 2);
}

void Ui::drawGalleryDialog(uint16_t* fb, int fbWidth, const Rect& clip) {
  if (galleryDialog_ == GalleryDialog::None || gallerySelected_ < 0 ||
      gallerySelected_ >= galleryCount_) {
    return;
  }

  // Modal dimming: the gallery remains visible as a dark backdrop, but the
  // confirmation panel and its two actions are clearly the focus.
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kModalDim);
  const Rect panel = ui_layout::kGalleryDialogPanel;
  gfx::fillRect(fb, fbWidth, clip, panel, kSheet);
  gfx::drawRect(fb, fbWidth, clip, panel, kAccent);
  gfx::drawRect(fb, fbWidth, clip,
                {static_cast<int16_t>(panel.x + 1),
                 static_cast<int16_t>(panel.y + 1),
                 static_cast<int16_t>(panel.w - 2),
                 static_cast<int16_t>(panel.h - 2)},
                kChromeLine);

  const bool load = galleryDialog_ == GalleryDialog::Load;
  gfx::drawText(fb, fbWidth, clip,
                {static_cast<int16_t>(panel.x + 16),
                 static_cast<int16_t>(panel.y + 16)},
                load ? "Open drawing?" : "Delete drawing?", kText, 2);
  drawTextCentered(fb, fbWidth, clip,
                   {static_cast<int16_t>(panel.x + 8),
                    static_cast<int16_t>(panel.y + 54),
                    static_cast<int16_t>(panel.w - 16), 24},
                   galleryNames_[gallerySelected_], kWhite, 2);

  const char* hint = load ? "Open this drawing?" : "This cannot be undone.";
  if (load && documentDirty_) {
    hint = "Unsaved changes will be lost.";
  }
  drawTextCentered(fb, fbWidth, clip,
                   {static_cast<int16_t>(panel.x + 8),
                    static_cast<int16_t>(panel.y + 92),
                    static_cast<int16_t>(panel.w - 16), 24},
                   hint, kTextDim, 2);

  gfx::fillRect(fb, fbWidth, clip, ui_layout::kGalleryCancel, kButton);
  gfx::drawRect(fb, fbWidth, clip, ui_layout::kGalleryCancel, kButtonBorder);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kGalleryCancel, "Cancel", kText,
                   2);

  const Rect action = load ? ui_layout::kGalleryOpen : ui_layout::kGalleryDelete;
  gfx::fillRect(fb, fbWidth, clip, action,
                load ? kButtonActive : rgb565(128, 32, 32));
  gfx::drawRect(fb, fbWidth, clip, action, load ? kAccent : kWhite);
  drawTextCentered(fb, fbWidth, clip, action, load ? "Open" : "Delete", kText, 2);
}

void Ui::drawSafeMode(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kSplashBg);
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kTopBar, kChrome);
  gfx::hline(fb, fbWidth, clip, 0, config::kTopBarHeight - 1,
             config::kScreenWidth, kChromeLine);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kTopBar, "Recovery mode",
                   kText, 3);

  const Rect panel = ui_layout::kSafePanel;
  gfx::fillRect(fb, fbWidth, clip, panel, kSheet);
  gfx::drawRect(fb, fbWidth, clip, panel, kAccent);

  drawTextCentered(fb, fbWidth, clip, {32, 76, 256, 24}, "Safe mode", kText, 2);
  drawTextCentered(fb, fbWidth, clip, {32, 104, 256, 20},
                   "Wi-Fi and autosave are off", kTextDim, 1);

  const struct {
    const char* label;
    bool ok;
  } status[3] = {
      {"Display", safeDisplayOk_},
      {"Touch", safeTouchOk_},
      {"Storage", safeStorageOk_},
  };
  for (int i = 0; i < 3; ++i) {
    char line[40];
    std::snprintf(line, sizeof(line), "%s: %s", status[i].label,
                  status[i].ok ? "OK" : "FAIL");
    drawTextLeftCentered(fb, fbWidth, clip,
                         {32, static_cast<int16_t>(124 + i * 22), 256, 20},
                         line, status[i].ok ? kText : kDanger, 2, 0);
  }

  drawButtonFrame(fb, fbWidth, clip, ui_layout::kSafeDisplayTest,
                  pressedSafeDisplay_, false, false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kSafeDisplayTest,
                   "Display test", kText, 2);
  drawButtonFrame(fb, fbWidth, clip, ui_layout::kSafeTouchTest, pressedSafeTouch_,
                  false, false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kSafeTouchTest, "Touch test",
                   kText, 2);
  drawButtonFrame(fb, fbWidth, clip, ui_layout::kSafeStorageTest,
                  pressedSafeStorage_, false, false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kSafeStorageTest,
                   "Storage test", kText, 2);

  drawButtonFrame(fb, fbWidth, clip, ui_layout::kSafeContinue,
                  pressedSafeContinue_, false, false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kSafeContinue, "Continue",
                   kText, 2);
  drawButtonFrame(fb, fbWidth, clip, ui_layout::kSafeReboot, pressedSafeReboot_,
                  false, false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kSafeReboot, "Restart",
                   kText, 2);
}

void Ui::drawDisplayTest(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kBlack);
  const uint16_t colors[6] = {kRed, kGreen, kBlue, kYellow, kWhite, kBlack};
  for (int i = 0; i < 6; ++i) {
    gfx::fillRect(fb, fbWidth, clip,
                  {0, static_cast<int16_t>(48 + i * 60), 320, 60}, colors[i]);
  }
  gfx::drawText(fb, fbWidth, clip, {12, 12}, "Display test", kWhite, 2);
  drawButtonFrame(fb, fbWidth, clip, ui_layout::kDisplayTestBack, false, false,
                  false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kDisplayTestBack, "Back",
                   kText, 2);
}

void Ui::drawTouchTest(uint16_t* fb, int fbWidth, const Rect& clip) {
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kScreen, kSplashBg);
  gfx::fillRect(fb, fbWidth, clip, ui_layout::kTopBar, kChrome);
  gfx::hline(fb, fbWidth, clip, 0, config::kTopBarHeight - 1,
             config::kScreenWidth, kChromeLine);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kTopBar, "Touch test", kText,
                   3);

  const Rect area = ui_layout::kTouchTestArea;
  gfx::drawRect(fb, fbWidth, clip, area, kButtonBorder);
  if (touchTestSeen_) {
    constexpr int arm = 18;
    gfx::hline(fb, fbWidth, clip, touchTestX_ - arm, touchTestY_, arm * 2 + 1,
               kAccent);
    gfx::vline(fb, fbWidth, clip, touchTestX_, touchTestY_ - arm, arm * 2 + 1,
               kAccent);
    gfx::drawCircle(fb, fbWidth, clip, {touchTestX_, touchTestY_}, 10, kWhite);

    char line[48];
    std::snprintf(line, sizeof(line), "x=%d  y=%d", touchTestX_, touchTestY_);
    gfx::drawText(fb, fbWidth, clip, {20, 372}, line, kText, 2);
    std::snprintf(line, sizeof(line), "raw %u,%u",
                  static_cast<unsigned>(touchTestRawX_),
                  static_cast<unsigned>(touchTestRawY_));
    gfx::drawText(fb, fbWidth, clip, {20, 394}, line, kTextDim, 2);
  } else {
    drawTextCentered(fb, fbWidth, clip, area, "Touch anywhere", kTextDim, 2);
  }

  drawButtonFrame(fb, fbWidth, clip, ui_layout::kTouchTestBack, false, false,
                  false);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kTouchTestBack, "Back", kText,
                   2);
}

void Ui::drawRecovery(uint16_t* fb, int fbWidth, const Rect& clip) {
  const Rect panel = ui_layout::kRecoveryPanel;
  gfx::fillRect(fb, fbWidth, clip, panel, kSheet);
  gfx::drawRect(fb, fbWidth, clip, panel, kAccent);

  drawTextCentered(
      fb, fbWidth, clip,
      {static_cast<int16_t>(panel.x + 8), static_cast<int16_t>(panel.y + 28),
       static_cast<int16_t>(panel.w - 16), 30},
      "Unsaved drawing", kText, 3);
  drawTextCentered(
      fb, fbWidth, clip,
      {static_cast<int16_t>(panel.x + 8), static_cast<int16_t>(panel.y + 70),
       static_cast<int16_t>(panel.w - 16), 24},
      "found in autosave", kTextDim, 2);
  drawTextCentered(
      fb, fbWidth, clip,
      {static_cast<int16_t>(panel.x + 8), static_cast<int16_t>(panel.y + 116),
       static_cast<int16_t>(panel.w - 16), 24},
      "Restore it or discard it.", kText, 2);
  drawTextCentered(
      fb, fbWidth, clip,
      {static_cast<int16_t>(panel.x + 8), static_cast<int16_t>(panel.y + 150),
       static_cast<int16_t>(panel.w - 16), 24},
      "The autosave is kept until", kTextDim, 2);
  drawTextCentered(
      fb, fbWidth, clip,
      {static_cast<int16_t>(panel.x + 8), static_cast<int16_t>(panel.y + 174),
       static_cast<int16_t>(panel.w - 16), 24},
      "you save or discard.", kTextDim, 2);

  gfx::fillRect(fb, fbWidth, clip, ui_layout::kRecoveryRestore, kButtonActive);
  gfx::drawRect(fb, fbWidth, clip, ui_layout::kRecoveryRestore, kAccent);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kRecoveryRestore, "Restore",
                   kText, 2);

  gfx::fillRect(fb, fbWidth, clip, ui_layout::kRecoveryDiscard,
                rgb565(128, 32, 32));
  gfx::drawRect(fb, fbWidth, clip, ui_layout::kRecoveryDiscard, kWhite);
  drawTextCentered(fb, fbWidth, clip, ui_layout::kRecoveryDiscard, "Discard",
                   kText, 2);
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
