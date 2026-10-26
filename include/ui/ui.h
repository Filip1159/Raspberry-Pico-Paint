#pragma once

// Immediate-mode UI shell: layout, hit testing, screens, bottom sheets and
// toasts. The class owns only UI state; rendering is a pure composition into
// the caller's RGB565 framebuffer. Layout constants are Arduino-free so the
// native tests can validate them.

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "display/dirty.h"
#include "input/touch_state.h"
#include "net/net.h"
#include "paint/geometry.h"
#include "storage/paint_files.h"
#include "storage/storage.h"

namespace paint {

enum class Screen : uint8_t {
  Splash,
  Canvas,
  Gallery,
  Settings,
  Storage,
  Network,
  Calibrate,
  About,
  Recovery,
  SafeMode,
  DisplayTest,
  TouchTest,
};

enum class Sheet : uint8_t {
  None,
  Tools,
  Palette,
  Size,
  Menu,
  ConfirmNew,
  SaveAs,
  Hsv,
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

// Clamp a screen-space point into the drawable canvas. Used for shapes and
// strokes that start inside the canvas but are dragged over the toolbar or
// beyond an edge. Native tests pin the corners.
constexpr Point clampCanvasPoint(Point p) {
  int x = p.x;
  int y = p.y;
  if (x < kCanvas.x) {
    x = kCanvas.x;
  }
  if (x >= kCanvas.right()) {
    x = kCanvas.right() - 1;
  }
  if (y < kCanvas.y) {
    y = kCanvas.y;
  }
  if (y >= kCanvas.bottom()) {
    y = kCanvas.bottom() - 1;
  }
  return Point{static_cast<int16_t>(x), static_cast<int16_t>(y)};
}

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

// Settings. Every actionable control is at least 44 px high; the two-column
// secondary actions keep the whole screen reachable without scrolling.
constexpr Rect kRotationButton{20, 64, 280, 44};
constexpr Rect kBrightnessDownButton{20, 116, 44, 44};
constexpr Rect kBrightnessUpButton{256, 116, 44, 44};
constexpr Rect kBrightnessRow{20, 116, 280, 44};
constexpr Rect kSettingsWifiToggle{20, 168, 280, 44};
constexpr Rect kCalibrateButton{20, 220, 136, 44};
constexpr Rect kStorageDetailsButton{164, 220, 136, 44};
constexpr Rect kAboutButton{20, 272, 136, 44};
constexpr Rect kSettingsNetworkButton{164, 272, 136, 44};
constexpr Rect kCalibrationCancel{120, 432, 80, 44};

// Storage gauge. It sits below the Settings actions and above the version
// footer.
constexpr Rect kStorageGaugeArea{0, 320, config::kScreenWidth, 128};
constexpr Rect kStorageBar{20, 360, 280, 16};

// Recovery / safe mode. The status rows are text, then one 44 px action per
// test followed by Continue and Restart.
constexpr Rect kSafePanel{20, 64, 280, 392};
constexpr Rect kSafeDisplayTest{32, 200, 256, 44};
constexpr Rect kSafeTouchTest{32, 252, 256, 44};
constexpr Rect kSafeStorageTest{32, 304, 256, 44};
constexpr Rect kSafeContinue{32, 388, 120, 44};
constexpr Rect kSafeReboot{168, 388, 120, 44};
constexpr Rect kDisplayTestBack{20, 420, 280, 44};
constexpr Rect kTouchTestArea{0, 48, config::kScreenWidth, 364};
constexpr Rect kTouchTestBack{20, 420, 280, 44};

// Wi-Fi & OTA screen. The text rows occupy 64..320 and the toggle sits at the
// same bottom position as the other full-width placeholder actions.
constexpr Rect kNetworkInfoArea{0, 64, config::kScreenWidth, 320};
constexpr Rect kNetworkSignalArea{0, 188, config::kScreenWidth, 28};
constexpr Rect kNetworkStorageArea{0, 244, config::kScreenWidth, 28};
constexpr Rect kNetworkToggleButton{20, 388, 280, 44};

// Firmware-update overlay: an opaque panel and a 240 px progress bar. The
// progress callback composes and flushes just this rectangle while
// ArduinoOTA's blocking receive loop keeps the main loop out of the way.
constexpr Rect kOtaPanel{20, 168, 280, 144};
constexpr Rect kOtaProgressBar{40, 248, 240, 18};

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

// The tools sheet grew upwards to make room for the contextual options row
// (size + outline/filled) below the seven tool cells.
constexpr Rect kSheetTools{0, 260, 320, 220};
constexpr Rect kSheetPalette{0, 260, 320, 220};
constexpr Rect kSheetSize{0, 360, 320, 120};
constexpr Rect kSheetMenu{0, 140, 320, 340};
constexpr Rect kSheetConfirmNew{0, 240, 320, 240};
constexpr Rect kSheetHsv{0, 110, 320, 370};

// HSV sheet controls.
constexpr Rect kHsvPreview{20, 144, 56, 56};
constexpr Rect kHsvHueBar{20, 222, 280, 24};
constexpr Rect kHsvSquare{60, 268, 200, 140};
constexpr Rect kHsvCancel{32, 424, 110, 44};
constexpr Rect kHsvOk{178, 424, 110, 44};
// Repaint bounds include the hue marker line, the S/V marker circles and the
// text row. Dirtying these smaller rectangles avoids re-flushing the whole
// 320x370 sheet for every tap.
constexpr Rect kHsvReadout{88, 144, 220, 56};
constexpr Rect kHsvHuePaint{20, 219, 280, 30};
constexpr Rect kHsvSquarePaint{52, 260, 216, 156};

// Bounds around the hue marker line, including its 3 px overshoot. A hue tap
// repaints only the gradients under the old and new markers instead of the
// whole 280x30 bar.
constexpr Rect hsvHueMarkerRect(uint8_t hue) {
  const int x = kHsvHueBar.x + (hue * (kHsvHueBar.w - 1)) / 255;
  return Rect{static_cast<int16_t>(x - 3),
              static_cast<int16_t>(kHsvHueBar.y - 3), 7,
              static_cast<int16_t>(kHsvHueBar.h + 6)};
}

// Bounds around the saturation/value marker (radius 8 plus a 2 px margin).
// Moving the marker dirties only its old and new bounds; the square's colours
// do not change when S/V changes, so the full 216x156 area can stay untouched.
constexpr Rect hsvMarkerRect(uint8_t saturation, uint8_t value) {
  const int x = kHsvSquare.x + (saturation * (kHsvSquare.w - 1)) / 255;
  const int y = kHsvSquare.y + ((255 - value) * (kHsvSquare.h - 1)) / 255;
  return Rect{static_cast<int16_t>(x - 10), static_cast<int16_t>(y - 10), 21, 21};
}

// New-canvas / Save-as confirmation controls.
constexpr Rect kConfirmNewCancel{32, 404, 110, 44};
constexpr Rect kConfirmNewOk{178, 404, 110, 44};

// Gallery: 2x2 cells with room for a filename under each thumbnail. The
// bottom row holds the page arrows and a page counter.
constexpr int kGalleryColumns = 2;
constexpr int kGalleryRows = 2;
constexpr int kGalleryPerPage = kGalleryColumns * kGalleryRows;
constexpr Rect kGalleryItems[kGalleryPerPage] = {
    {20, 76, 130, 168},
    {170, 76, 130, 168},
    {20, 256, 130, 168},
    {170, 256, 130, 168},
};
constexpr Rect kGalleryPrev{20, 436, 44, 44};
constexpr Rect kGalleryNext{256, 436, 44, 44};
constexpr Rect kGalleryPageLabel{72, 442, 176, 32};

// Load/delete confirmation panel drawn over the gallery.
constexpr Rect kGalleryDialogPanel{20, 220, 280, 208};
constexpr Rect kGalleryCancel{32, 368, 110, 44};
constexpr Rect kGalleryOpen{178, 368, 110, 44};
constexpr Rect kGalleryDelete{178, 368, 110, 44};

// Boot recovery prompt controls.
constexpr Rect kRecoveryPanel{20, 100, 280, 280};
constexpr Rect kRecoveryRestore{32, 316, 110, 44};
constexpr Rect kRecoveryDiscard{178, 316, 110, 44};

constexpr int galleryPageCount(int fileCount) {
  return fileCount <= 0 ? 1 : (fileCount + kGalleryPerPage - 1) / kGalleryPerPage;
}

constexpr int hitGalleryItem(Point p) {
  for (int i = 0; i < kGalleryPerPage; ++i) {
    if (kGalleryItems[i].contains(p)) {
      return i;
    }
  }
  return -1;
}

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
    case Sheet::ConfirmNew:
    case Sheet::SaveAs:
      return kSheetConfirmNew;
    case Sheet::Hsv:
      return kSheetHsv;
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

// Tools sheet: three columns, three rows. The last row has one cell; the
// seventh slot is unused. Every cell is 44 px high. The bottom row holds the
// contextual options: size, outline and filled.
constexpr Rect toolItemRect(int index) {
  if (index < 0 || index >= 7) {
    return Rect{};
  }
  const int col = index % 3;
  const int row = index / 3;
  return Rect{static_cast<int16_t>(16 + col * 100),
              static_cast<int16_t>(296 + row * 44), 96, 44};
}

constexpr Rect kToolsOptionsRow{0, 432, 320, 48};
constexpr Rect kToolsSizeButton{16, 432, 96, 44};
constexpr Rect kToolsOutlineButton{118, 432, 92, 44};
constexpr Rect kToolsFilledButton{216, 432, 90, 44};

// Returns 0 for size, 1 for outline, 2 for filled, or -1.
constexpr int hitToolOption(Point p) {
  if (kToolsSizeButton.contains(p)) {
    return 0;
  }
  if (kToolsOutlineButton.contains(p)) {
    return 1;
  }
  if (kToolsFilledButton.contains(p)) {
    return 2;
  }
  return -1;
}

// Palette sheet: 6x4 swatches plus an HSV shortcut in the title row. The
// swatch cells are 44x44; the colour itself fills the whole cell so the
// target and the visual shape agree.
constexpr Rect paletteSwatchRect(int index) {
  if (index < 0 || index >= 24) {
    return Rect{};
  }
  const int col = index % 6;
  const int row = index / 6;
  return Rect{static_cast<int16_t>(28 + col * 44),
              static_cast<int16_t>(300 + row * 44), 44, 44};
}

constexpr Rect kPaletteHsvButton{244, 268, 60, 24};
// Expand the HSV shortcut's touch target to 44 px high without changing its
// compact visual label.
constexpr Rect kPaletteHsvHit{244, 258, 60, 44};

// Five recent colours live in the palette title row, between the title and
// the HSV shortcut.
constexpr int kRecentColorCount = 5;
constexpr Rect paletteRecentRect(int index) {
  if (index < 0 || index >= kRecentColorCount) {
    return Rect{};
  }
  return Rect{static_cast<int16_t>(124 + index * 22), 272, 18, 18};
}

constexpr int hitPaletteRecent(Point p) {
  for (int i = 0; i < kRecentColorCount; ++i) {
    if (paletteRecentRect(i).contains(p)) {
      return i;
    }
  }
  return -1;
}

// Brush-size sheet: seven 44x44 cells with no gap, so each target is
// comfortably tappable.
constexpr Rect sizeItemRect(int index) {
  if (index < 0 || index >= 7) {
    return Rect{};
  }
  return Rect{static_cast<int16_t>(6 + index * 44), 420, 44, 44};
}

// Menu sheet: seven full-width 44 px rows. The 44 px pitch fills the 340 px
// sheet and gives every row an accessible target.
constexpr Rect menuItemRect(int index) {
  if (index < 0 || index >= 7) {
    return Rect{};
  }
  return Rect{16, static_cast<int16_t>(172 + index * 44), 288, 44};
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
      for (int i = 0; i < 7; ++i) {
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

constexpr bool hitPaletteHsv(Point p) { return kPaletteHsvHit.contains(p); }

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
    case Sheet::Hsv:
      return 2;
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
  void compose(uint16_t* fb, int fbWidth, const Rect& clip);

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
  bool shapeFilled() const { return shapeFilled_; }
  bool toastVisible() const { return toastActive_; }

  // Gallery and document state. The UI owns presentation only; the main loop
  // scans LittleFS, saves/loads/deletes files and supplies thumbnail pixels.
  using ThumbnailRenderer = bool (*)(void* context, uint16_t* fb, int fbWidth,
                                     const Rect& clip, const Rect& dest,
                                     const char* fileName);
  void setThumbnailRenderer(ThumbnailRenderer renderer, void* context) {
    thumbnailRenderer_ = renderer;
    thumbnailContext_ = context;
  }
  void setGalleryFiles(const char* const* names, int count);
  void setDocument(const char* name, bool dirty);
  const char* documentName() const { return documentName_; }
  bool documentDirty() const { return documentDirty_; }
  void setStorageStats(const StorageStats& stats);
  void setStorageFileCount(int count);
  bool hasOtaHeadroom() const { return storageStats_.otaHeadroom(); }

  // Wi-Fi / OTA status is supplied by the main loop. Changes only dirty the
  // Network content area, so the one-second RSSI refresh does not repaint the
  // top bar. The toggle is a one-shot request: main persists the setting and
  // applies it to Net.
  void setNetworkInfo(const NetInfo& info);
  const NetInfo& networkInfo() const { return networkInfo_; }
  bool takeNetworkToggleRequest() {
    const bool value = networkToggleRequested_;
    networkToggleRequested_ = false;
    return value;
  }

  // Persisted display settings shown on the Settings screen. Main owns the
  // actual hardware/persistence; the UI only renders and emits one-shot
  // requests.
  void setRotation(uint8_t rotation) {
    displayRotation_ = rotation == 0 ? 0 : config::kDisplayRotation;
    if (screen_ == Screen::Settings) {
      dirty_.add(ui_layout::kRotationButton);
    }
  }
  uint8_t rotation() const { return displayRotation_; }
  bool takeRotationToggleRequest() {
    const bool value = rotationToggleRequested_;
    rotationToggleRequested_ = false;
    return value;
  }

  void setBrightness(uint8_t brightness) {
    brightness_ = brightness;
    if (screen_ == Screen::Settings) {
      dirty_.add(ui_layout::kBrightnessRow);
    }
  }
  uint8_t brightness() const { return brightness_; }
  bool takeBrightnessStepRequest(int8_t& step) {
    if (!brightnessStepRequested_) {
      return false;
    }
    step = brightnessStep_;
    brightnessStepRequested_ = false;
    brightnessStep_ = 0;
    return true;
  }

  // Recovery / safe mode. Main decides whether the device is in recovery from
  // the boot-health counter or a touch held at power-on, then feeds the
  // self-test results back here.
  void setSafeMode(bool safeMode, bool displayOk, bool touchOk, bool storageOk) {
    safeMode_ = safeMode;
    safeDisplayOk_ = displayOk;
    safeTouchOk_ = touchOk;
    safeStorageOk_ = storageOk;
    if (safeMode_ && screen_ == Screen::Splash) {
      dirty_.add(ui_layout::kScreen);
    }
  }
  bool safeMode() const { return safeMode_; }
  bool takeSafeModeContinueRequest() {
    const bool value = safeContinueRequested_;
    safeContinueRequested_ = false;
    return value;
  }
  bool takeSafeModeRebootRequest() {
    const bool value = safeRebootRequested_;
    safeRebootRequested_ = false;
    return value;
  }

  // OTA progress is drawn from ArduinoOTA's callbacks, not from the normal
  // loop. beginOta() enters the overlay state; setOtaProgress() dirties the
  // panel, and the main callback composes/flushes it over the preserved canvas
  // synchronously.
  void beginOta();
  bool setOtaProgress(uint8_t percent);
  void endOta();
  bool otaActive() const { return otaActive_; }
  int galleryCount() const { return galleryCount_; }
  int galleryPage() const { return galleryPage_; }
  int galleryPageCount() const {
    return ui_layout::galleryPageCount(galleryCount_);
  }
  bool galleryDialogOpen() const { return galleryDialog_ != GalleryDialog::None; }

  // Save-as is a two-step flow: the UI asks the main loop for a free name,
  // then promptSaveAs() opens the confirmation sheet with that exact name.
  bool takeSaveAsRequest() {
    const bool value = saveAsRequested_;
    saveAsRequested_ = false;
    return value;
  }
  void promptSaveAs(const char* name);
  bool takeSaveRequest(char* out, size_t capacity);
  bool takeLoadRequest(char* out, size_t capacity);
  bool takeDeleteRequest(char* out, size_t capacity);

  // Boot recovery: main validates /paint/autosave.pnt and enables the prompt
  // before the splash ends. Either action returns a one-shot request.
  void setRecoveryAvailable(bool available) { recoveryAvailable_ = available; }
  bool takeRecoveryRestoreRequest() {
    const bool value = recoveryRestoreRequested_;
    recoveryRestoreRequested_ = false;
    return value;
  }
  bool takeRecoveryDiscardRequest() {
    const bool value = recoveryDiscardRequested_;
    recoveryDiscardRequested_ = false;
    return value;
  }

  // Return to the canvas after a successful load, recovery, discard or a
  // failed OTA. If the canvas is already current, close any open sheet rather
  // than leaving it over the restored drawing.
  void showCanvas() {
    recoveryAvailable_ = false;
    if (screen_ == Screen::Canvas) {
      setSheet(Sheet::None);
    } else {
      setScreen(Screen::Canvas);
    }
  }

  // Eyedropper: the canvas pixel becomes the current colour.
  void pickColor(uint16_t color, uint32_t nowMs);

  void setUndoRedoAvailable(bool canUndo, bool canRedo) {
    if (undoAvailable_ != canUndo) {
      undoAvailable_ = canUndo;
      dirty_.add(ui_layout::kButtons[3]);
    }
    if (redoAvailable_ != canRedo) {
      redoAvailable_ = canRedo;
      dirty_.add(ui_layout::kButtons[4]);
    }
  }

  bool takeUndoRequest() {
    const bool value = undoRequested_;
    undoRequested_ = false;
    return value;
  }

  bool takeRedoRequest() {
    const bool value = redoRequested_;
    redoRequested_ = false;
    return value;
  }

  bool takeNewCanvasRequest() {
    const bool value = newCanvasRequested_;
    newCanvasRequested_ = false;
    return value;
  }

  void notify(const char* text, uint32_t nowMs) { showToast(text, nowMs); }

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
  enum class GalleryDialog : uint8_t { None, Load, Delete };

  void setScreen(Screen next);
  void setSheet(Sheet next);
  void showToast(const char* text, uint32_t nowMs);
  void activateTopBar(int index, uint32_t nowMs);
  void handleCanvasTap(Point p, uint32_t nowMs);
  void activateSheetItem(Point p, uint32_t nowMs);
  void openHsv();
  void rememberColor(uint16_t color);
  void updatePlaceholder(const TouchFrame& frame, uint32_t nowMs);
  void updateNetwork(const TouchFrame& frame);
  void updateGallery(const TouchFrame& frame);
  void updateRecovery(const TouchFrame& frame);
  void updateSafeMode(const TouchFrame& frame);
  void updateTouchTest(const TouchFrame& frame);
  bool networkCanToggle() const;
  void setGalleryDialog(GalleryDialog next);
  const char* selectedGalleryName() const;
  void startCalibration();
  void updateCalibration(const TouchFrame& frame, uint32_t nowMs);

  void drawSplash(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawCanvasScreen(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawTopBar(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawSheet(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawPlaceholder(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawNetwork(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawOtaOverlay(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawGallery(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawGalleryDialog(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawRecovery(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawSafeMode(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawDisplayTest(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawTouchTest(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawCalibration(uint16_t* fb, int fbWidth, const Rect& clip);
  void drawToast(uint16_t* fb, int fbWidth, const Rect& clip);

  Screen screen_ = Screen::Splash;
  Screen backScreen_ = Screen::Canvas;
  Sheet sheet_ = Sheet::None;
  Tool tool_ = Tool::Brush;
  uint8_t brushSize_ = config::kDefaultBrushSize;
  uint16_t color_ = 0x0000;
  bool shapeFilled_ = false;
  int8_t pressedButton_ = -1;
  bool pressedBack_ = false;
  bool pressedCalibrate_ = false;
  bool pressedStorage_ = false;
  bool pressedNetwork_ = false;
  bool pressedRotation_ = false;
  bool pressedBrightnessDown_ = false;
  bool pressedBrightnessUp_ = false;
  bool pressedSettingsWifi_ = false;
  bool pressedAbout_ = false;
  bool pressedSettingsNetwork_ = false;
  bool pressedSafeDisplay_ = false;
  bool pressedSafeTouch_ = false;
  bool pressedSafeStorage_ = false;
  bool pressedSafeContinue_ = false;
  bool pressedSafeReboot_ = false;
  bool networkToggleRequested_ = false;
  NetInfo networkInfo_;
  uint8_t displayRotation_ = config::kDisplayRotation;
  uint8_t brightness_ = config::kDefaultBrightness;
  bool rotationToggleRequested_ = false;
  bool brightnessStepRequested_ = false;
  int8_t brightnessStep_ = 0;
  bool safeMode_ = false;
  bool safeDisplayOk_ = true;
  bool safeTouchOk_ = true;
  bool safeStorageOk_ = true;
  bool safeContinueRequested_ = false;
  bool safeRebootRequested_ = false;
  bool touchTestSeen_ = false;
  int16_t touchTestX_ = config::kScreenWidth / 2;
  int16_t touchTestY_ = config::kScreenHeight / 2;
  uint16_t touchTestRawX_ = 0;
  uint16_t touchTestRawY_ = 0;
  bool otaActive_ = false;
  uint8_t otaProgress_ = 0;
  bool touchCalibrated_ = false;
  bool toastActive_ = false;
  bool undoAvailable_ = false;
  bool redoAvailable_ = false;
  bool undoRequested_ = false;
  bool redoRequested_ = false;
  bool newCanvasRequested_ = false;
  uint8_t hsvH_ = 0;
  uint8_t hsvS_ = 0;
  uint8_t hsvV_ = 0;
  uint16_t recentColors_[ui_layout::kRecentColorCount] = {};
  uint8_t recentCount_ = 0;
  uint32_t toastUntil_ = 0;
  uint32_t splashUntil_ = 0;
  uint8_t calibrationStep_ = 0;
  CalibrationPoint calibrationPoints_[4] = {};
  Calibration calibrationResult_;
  bool calibrationComplete_ = false;
  char toast_[40] = {};
  DirtyRegion dirty_;

  ThumbnailRenderer thumbnailRenderer_ = nullptr;
  void* thumbnailContext_ = nullptr;
  char galleryNames_[kMaxPaintFiles][kPaintFileNameSize] = {};
  int galleryCount_ = 0;
  int galleryPage_ = 0;
  int gallerySelected_ = -1;
  GalleryDialog galleryDialog_ = GalleryDialog::None;
  bool saveAsRequested_ = false;
  bool saveRequested_ = false;
  char saveName_[kPaintFileNameSize] = {};
  bool loadRequested_ = false;
  char loadName_[kPaintFileNameSize] = {};
  bool deleteRequested_ = false;
  char deleteName_[kPaintFileNameSize] = {};
  char documentName_[kPaintFileNameSize] = {};
  bool documentDirty_ = false;
  StorageStats storageStats_;
  int storageFileCount_ = 0;
  bool recoveryAvailable_ = false;
  bool recoveryRestoreRequested_ = false;
  bool recoveryDiscardRequested_ = false;
};

}  // namespace paint
