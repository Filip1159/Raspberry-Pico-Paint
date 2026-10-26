#include <Arduino.h>
#include <LittleFS.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "config.h"
#include "display/dirty.h"
#include "display/gfx.h"
#include "display/ili9488.h"
#include "input/calibration.h"
#include "input/orientation.h"
#include "input/touch.h"
#include "input/touch_state.h"
#include "net/net.h"
#include "paint/brush.h"
#include "paint/canvas.h"
#include "paint/color.h"
#include "paint/shapes.h"
#include "paint/stroke.h"
#include "paint/undo.h"
#include "storage/autosave.h"
#include "storage/boot_health.h"
#include "storage/paint_files.h"
#include "storage/pnt_file.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "ui/idle.h"
#include "ui/ui.h"
#include "version.h"

namespace {

constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kLedBlinkMs = 500;
constexpr int kFbWidth = paint::config::kScreenWidth;
constexpr int kFbHeight = paint::config::kScreenHeight;
constexpr int kOverlayBandHeight = 32;

const paint::Rect kScreenRect = {0, 0, kFbWidth, kFbHeight};

paint::Ili9488 gDisplay;
#ifdef PAINT_DIAG
paint::DirtyRegion gDirty;
#endif
paint::Xpt2046Touch gTouch;
paint::TouchStateMachine gTouchState;
paint::Ui gUi;
paint::Settings gSettings;
paint::Storage gStorage;
paint::PaintFileIndex gPaintFiles;
paint::Net gNet;
bool gFilesystemReady = false;
size_t gStorageFreeBytes = 0;
// ArduinoOTA's authentication error callback can run in the lwIP stack
// context, so it only records a request. The main loop owns all UI/display
// access and shows the error after Net::loop() returns.
volatile bool gOtaErrorPending = false;
volatile paint::OtaError gOtaErrorCode = paint::OtaError::None;
char gDocumentName[paint::kPaintFileNameSize] = {};
bool gDocumentDirty = false;
paint::AutosaveTimer gAutosave;
uint16_t* gFramebuffer = nullptr;
uint16_t* gOverlayScratch = nullptr;
paint::Canvas gCanvas;
paint::StrokeController gStroke;
paint::UndoStack gUndo;
paint::ShapePreview gShape;
paint::ShapeKind gShapeKind = paint::ShapeKind::Line;
paint::Point gShapeStart;
bool gBaseReady = false;
bool gSettingsLoaded = false;
bool gDisplayOk = false;
bool gTouchOk = false;
bool gSafeMode = false;
bool gNetworkSuppressed = false;
bool gAutosaveSuppressed = false;
bool gBootHealthCleared = false;
bool gBootHealthClearTried = false;
bool gSplashHandled = false;
bool gWakeConsume = false;
paint::IdleController gIdle;
uint32_t gBootMs = 0;
#ifdef PAINT_DIAG
uint32_t gStrokeDownUs = 0;

void printUndoUsage(const char* tag) {
  const paint::UndoStack::Usage usage = gUndo.usage();
  Serial.printf("[undo] %s actions=%d records=%d bytes=%d\n", tag,
                usage.actions, usage.records, usage.bytes);
}
#endif

void flushDisplayRect(const paint::Rect& rect) {
  if (!rect.empty()) {
    gDisplay.flush(rect, gFramebuffer, kFbWidth);
  }
}

void normalizeSettings() {
  gSettings.rotation = paint::normalizeRotation(gSettings.rotation);

  // Older configs may hold an arbitrary duty value. Snap to the four exposed
  // levels so the label and the +/- steps stay consistent.
  uint8_t best = paint::config::kBrightnessLevels[0];
  uint16_t bestDistance = 0xFFFF;
  for (uint8_t i = 0; i < paint::config::kBrightnessLevelCount; ++i) {
    const uint8_t level = paint::config::kBrightnessLevels[i];
    const uint16_t distance = gSettings.brightness > level
                                  ? gSettings.brightness - level
                                  : level - gSettings.brightness;
    if (distance < bestDistance) {
      best = level;
      bestDistance = distance;
    }
  }
  gSettings.brightness = best;
}

uint8_t brightnessIndex(uint8_t value) {
  uint8_t best = 0;
  uint32_t bestDistance = 0xFFFFFFFFu;
  for (uint8_t i = 0; i < paint::config::kBrightnessLevelCount; ++i) {
    const int32_t diff =
        static_cast<int32_t>(value) - paint::config::kBrightnessLevels[i];
    const uint32_t distance =
        static_cast<uint32_t>(diff < 0 ? -diff : diff);
    if (distance < bestDistance) {
      best = i;
      bestDistance = distance;
    }
  }
  return best;
}

void applyBacklight() {
  uint8_t duty = gSettings.brightness;
  if (gIdle.level() == paint::IdleLevel::Dimmed) {
    duty = paint::config::kIdleDimDuty;
  } else if (gIdle.level() == paint::IdleLevel::Off) {
    duty = 0;
  }
  gDisplay.setBacklight(duty);
}

bool saveAndApplySettings() {
  if (!gFilesystemReady) {
    return false;
  }
  const bool saved = paint::saveSettings(gSettings);
  if (!saved) {
    Serial.println("[cfg] settings save failed");
  }
  return saved;
}

paint::Point mapTouchForDisplay(paint::Point point) {
  return paint::mapCanonicalToDisplay(point, gSettings.rotation);
}

uint16_t* canvasPixels() {
  const paint::Rect& bounds = gCanvas.bounds();
  return gCanvas.pixels() + static_cast<size_t>(bounds.y) * gCanvas.stride() +
         bounds.x;
}

void updateStorageStats() {
  if (!gFilesystemReady) {
    gStorageFreeBytes = 0;
    gUi.setStorageStats(paint::StorageStats{});
    return;
  }
  const paint::StorageStats stats = gStorage.stats();
  gStorageFreeBytes = stats.freeBytes;
  gUi.setStorageStats(stats);
}

void clearAutosave() {
  gAutosave.reset();
  if (!gFilesystemReady || !LittleFS.exists(paint::kAutosavePath)) {
    return;
  }
  if (!LittleFS.remove(paint::kAutosavePath)) {
    Serial.println("[autosave] could not remove old autosave");
  } else {
    updateStorageStats();
  }
}

void setDocumentState(const char* name, bool dirty) {
  const bool wasDirty = gDocumentDirty;
  std::snprintf(gDocumentName, sizeof(gDocumentName), "%s", name ? name : "");
  gDocumentDirty = dirty;
  if (!dirty) {
    gAutosave.reset();
  } else if (!wasDirty) {
    gAutosave.changed(millis());
  }
  gUi.setDocument(gDocumentName, dirty);
}

void rememberLastOpened(const char* name) {
  const char* next = name ? name : "";
  if (std::strcmp(gSettings.lastOpened, next) == 0) {
    return;
  }
  std::snprintf(gSettings.lastOpened, sizeof(gSettings.lastOpened), "%s", next);
  if (!gFilesystemReady) {
    return;
  }
  if (!paint::saveSettings(gSettings)) {
    Serial.println("[cfg] failed to persist last-opened file");
  } else {
    updateStorageStats();
  }
}

void markDocumentDirty() {
  if (!gDocumentDirty) {
    gDocumentDirty = true;
    gUi.setDocument(gDocumentName, true);
  }
  gAutosave.changed(millis());
}

bool writeAutosaveNow() {
  if (!gFilesystemReady) {
    return false;
  }
  const paint::Rect& bounds = gCanvas.bounds();
  if (!paint::savePnt(paint::kAutosavePath, canvasPixels(), gCanvas.stride(),
                      static_cast<uint16_t>(bounds.w),
                      static_cast<uint16_t>(bounds.h))) {
    return false;
  }
  gAutosave.saved();
  updateStorageStats();
  return true;
}

void autosaveIfDue(uint32_t now) {
  if (!gFilesystemReady || !gDocumentDirty || !gAutosave.pending()) {
    return;
  }
  // A shape preview is painted straight into the canvas; wait until the drag
  // ends so the rubber band is never captured in the autosave.
  if (gShape.active() || gStroke.active() || !gAutosave.due(now)) {
    return;
  }

  if (writeAutosaveNow()) {
    Serial.println("[autosave] saved /paint/autosave.pnt");
  } else {
    gAutosave.retry(now);
    Serial.println("[autosave] failed");
  }
}

void refreshGallery() {
  if (!gFilesystemReady) {
    gUi.setGalleryFiles(nullptr, 0);
    gUi.setStorageFileCount(0);
    return;
  }

  const int count = gPaintFiles.refresh();
  const char* names[paint::kMaxPaintFiles];
  for (int i = 0; i < count; ++i) {
    names[i] = gPaintFiles.name(i);
  }
  gUi.setGalleryFiles(names, count);
  gUi.setStorageFileCount(gPaintFiles.totalCount());
}

bool renderPntThumbnail(void*, uint16_t* fb, int fbWidth, const paint::Rect& clip,
                        const paint::Rect& dest, const char* name) {
  char path[32];
  return paint::paintFilePath(name, path, sizeof(path)) &&
         paint::drawPntThumbnail(path, fb, fbWidth, clip, dest);
}

bool rectInside(const paint::Rect& inner, const paint::Rect& outer) {
  return !inner.empty() && !outer.empty() && inner.x >= outer.x &&
         inner.y >= outer.y && inner.right() <= outer.right() &&
         inner.bottom() <= outer.bottom();
}

// Compose a dirty rectangle that belongs to the persistent base layer. The
// top bar is live UI state and is composed before flushing; canvas pixels are
// already in the framebuffer and are flushed as-is.
void composeBaseRect(const paint::Rect& rect) {
  if (rect.empty()) {
    return;
  }

  const paint::Rect top = rect.intersect(paint::ui_layout::kTopBar);
  if (!top.empty()) {
    gUi.compose(gFramebuffer, kFbWidth, top);
    flushDisplayRect(top);
  }

  const paint::Rect bottom = rect.intersect(paint::ui_layout::kCanvas);
  if (!bottom.empty()) {
    flushDisplayRect(bottom);
  }
}

// Show an opaque UI overlay (sheet, full-screen screen, or toast) without
// losing the canvas pixels underneath. Each horizontal band is composed,
// flushed, and then restored from a small scratch buffer.
void presentOverlayRect(const paint::Rect& rect) {
  if (rect.empty() || !gOverlayScratch) {
    return;
  }

  for (int y = rect.y; y < rect.bottom(); y += kOverlayBandHeight) {
    int h = rect.bottom() - y;
    if (h > kOverlayBandHeight) {
      h = kOverlayBandHeight;
    }
    if (h <= 0) {
      break;
    }

    for (int row = 0; row < h; ++row) {
      std::memcpy(gOverlayScratch + row * rect.w,
                  gFramebuffer + (y + row) * kFbWidth + rect.x,
                  static_cast<size_t>(rect.w) * sizeof(uint16_t));
    }

    const paint::Rect band{rect.x, static_cast<int16_t>(y), rect.w,
                           static_cast<int16_t>(h)};
    const uint32_t start = micros();
    gUi.compose(gFramebuffer, kFbWidth, band);
    flushDisplayRect(band);
    const uint32_t elapsed = micros() - start;

    for (int row = 0; row < h; ++row) {
      std::memcpy(gFramebuffer + (y + row) * kFbWidth + rect.x,
                  gOverlayScratch + row * rect.w,
                  static_cast<size_t>(rect.w) * sizeof(uint16_t));
    }

#ifdef PAINT_DIAG
    Serial.printf("[ui] overlay band %d,%d %dx%d: %lu us\n", band.x, band.y,
                  band.w, band.h, static_cast<unsigned long>(elapsed));
#endif
  }
}

void presentDirtyRect(const paint::Rect& rect) {
  if (rect.empty()) {
    return;
  }

  paint::Rect cover;
  if (gUi.screen() == paint::Screen::Splash) {
    gUi.compose(gFramebuffer, kFbWidth, rect);
    flushDisplayRect(rect);
    return;
  }
  if (gUi.screen() != paint::Screen::Canvas) {
    cover = kScreenRect;
  } else if (gUi.sheet() != paint::Sheet::None) {
    cover = paint::ui_layout::sheetRect(gUi.sheet());
  } else if (gUi.toastVisible()) {
    cover = paint::ui_layout::kToast;
  }

  if (cover.empty()) {
    composeBaseRect(rect);
    return;
  }

  const paint::Rect inside = rect.intersect(cover);
  if (inside.empty()) {
    composeBaseRect(rect);
    return;
  }
  if (inside == rect) {
    presentOverlayRect(rect);
    return;
  }

  // Flush the parts outside the opaque overlay from the base framebuffer,
  // then compose the covered part.
  if (inside.y > rect.y) {
    composeBaseRect({rect.x, rect.y, rect.w,
                     static_cast<int16_t>(inside.y - rect.y)});
  }
  if (inside.bottom() < rect.bottom()) {
    composeBaseRect({rect.x, inside.bottom(), rect.w,
                     static_cast<int16_t>(rect.bottom() - inside.bottom())});
  }
  if (inside.x > rect.x) {
    composeBaseRect({rect.x, inside.y,
                     static_cast<int16_t>(inside.x - rect.x), inside.h});
  }
  if (inside.right() < rect.right()) {
    composeBaseRect({inside.right(), inside.y,
                     static_cast<int16_t>(rect.right() - inside.right()),
                     inside.h});
  }
  presentOverlayRect(inside);
}

void initializeCanvasBase() {
  gCanvas.setTarget(gFramebuffer, kFbWidth, paint::ui_layout::kCanvas);
  gCanvas.clear(paint::config::kPaperColor);
  gCanvas.clearDirty();

  gUi.compose(gFramebuffer, kFbWidth, kScreenRect);
  flushDisplayRect(kScreenRect);
  gUi.clearDirty();
  gUndo.clear();
  gUi.setUndoRedoAvailable(false, false);
  gBaseReady = true;
  Serial.println("[paint] canvas layer ready");
}

void flushCanvasDirty() {
#ifdef PAINT_DIAG
  const uint32_t start = micros();
#endif
  paint::Rect avoid;
  if (gUi.screen() == paint::Screen::Canvas && gUi.toastVisible()) {
    avoid = paint::ui_layout::kToast;
  }

  const int count = gCanvas.dirtyCount();
  for (int i = 0; i < count; ++i) {
    const paint::Rect rect = gCanvas.dirtyRect(i);
    const paint::Rect inside = avoid.empty() ? paint::Rect{} : rect.intersect(avoid);
    if (inside.empty()) {
      flushDisplayRect(rect);
      continue;
    }
    if (inside.y > rect.y) {
      flushDisplayRect({rect.x, rect.y, rect.w,
                        static_cast<int16_t>(inside.y - rect.y)});
    }
    if (inside.bottom() < rect.bottom()) {
      flushDisplayRect({rect.x, inside.bottom(), rect.w,
                        static_cast<int16_t>(rect.bottom() - inside.bottom())});
    }
    if (inside.x > rect.x) {
      flushDisplayRect({rect.x, inside.y,
                        static_cast<int16_t>(inside.x - rect.x), inside.h});
    }
    if (inside.right() < rect.right()) {
      flushDisplayRect({inside.right(), inside.y,
                        static_cast<int16_t>(rect.right() - inside.right()),
                        inside.h});
    }
  }
#ifdef PAINT_DIAG
  static uint32_t lastReportMs = 0;
  static uint32_t flushes = 0;
  static uint32_t totalUs = 0;
  static uint32_t maxUs = 0;
  static uint32_t rectCount = 0;
  if (count > 0) {
    const uint32_t elapsed = micros() - start;
    ++flushes;
    totalUs += elapsed;
    if (elapsed > maxUs) {
      maxUs = elapsed;
    }
    rectCount += static_cast<uint32_t>(count);
  }
  const uint32_t nowMs = millis();
  if (nowMs - lastReportMs >= 1000) {
    if (flushes > 0) {
      Serial.printf("[paint] %lu flushes/s avg=%lu us max=%lu us rects=%lu\n",
                    static_cast<unsigned long>(flushes),
                    static_cast<unsigned long>(totalUs / flushes),
                    static_cast<unsigned long>(maxUs),
                    static_cast<unsigned long>(rectCount));
    }
    lastReportMs = nowMs;
    flushes = 0;
    totalUs = 0;
    maxUs = 0;
    rectCount = 0;
  }
#endif
  gCanvas.clearDirty();
}

paint::Point clampToCanvas(paint::Point p) {
  return paint::ui_layout::clampCanvasPoint(p);
}

bool isShapeTool(paint::Tool tool) {
  return tool == paint::Tool::Line || tool == paint::Tool::Rect ||
         tool == paint::Tool::Ellipse;
}

paint::ShapeKind shapeKindFor(paint::Tool tool) {
  switch (tool) {
    case paint::Tool::Rect:
      return paint::ShapeKind::Rect;
    case paint::Tool::Ellipse:
      return paint::ShapeKind::Ellipse;
    default:
      return paint::ShapeKind::Line;
  }
}

void cancelShape() {
  if (gShape.active()) {
    gShape.cancel(gCanvas);
  }
}

void finishUndoAction(uint32_t nowMs) {
  if (gUndo.takeOverflow()) {
    gUi.notify("Stroke too large to undo", nowMs);
  }
  gUi.setUndoRedoAvailable(gUndo.canUndo(), gUndo.canRedo());
}

void printBanner() {
  Serial.println();
  Serial.println("========================================");
  Serial.println("  Pico Paint");
  Serial.printf("  version : %s\n", PAINT_VERSION);
  Serial.printf("  built   : %s %s\n", PAINT_BUILD_DATE, PAINT_BUILD_TIME);
  Serial.printf("  board   : Pico 2 W (RP2350) @ %lu MHz\n",
                static_cast<unsigned long>(F_CPU / 1000000UL));
  Serial.printf("  display : ILI9488 %dx%d (portrait)\n",
                paint::config::kScreenWidth, paint::config::kScreenHeight);
  Serial.println("  stage   : M7 Wi-Fi + OTA");
  Serial.println("========================================");
}

// Return to a valid canvas layer without composing drawCanvasScreen() over
// the framebuffer. The canvas pixels live in the framebuffer itself, so a full
// compose would replace them with paper and lose the current drawing. Normally
// the base layer is already present; only the live top bar needs composing.
// Splash and boot-recovery have not established a canvas layer yet, so those
// paths still need the full initialize.
void showCanvasFromBase() {
  const paint::Screen previous = gUi.screen();
  gUi.showCanvas();

  if (!gBaseReady || previous == paint::Screen::Splash ||
      previous == paint::Screen::Recovery) {
    initializeCanvasBase();
    return;
  }

  const paint::Rect& top = paint::ui_layout::kTopBar;
  gUi.compose(gFramebuffer, kFbWidth, top);
  flushDisplayRect(top);
  flushDisplayRect(paint::ui_layout::kCanvas);
  gCanvas.clearDirty();
  gUi.clearDirty();
}

// Compose the OTA panel over the preserved canvas base, flush it, then put the
// underlying pixels back. This keeps the drawing intact if the update fails
// and also prevents the next progress callback from compositing stale panel
// pixels into the canvas framebuffer.
void drawOtaPanel() {
  presentOverlayRect(paint::ui_layout::kOtaPanel);
  gUi.clearDirty();
}

void onOtaStart(void*) {
  const uint32_t startMs = millis();
  Serial.println("[ota] update starting");
  cancelShape();
  if (gStroke.active()) {
    gStroke.cancel();
    gUndo.commit();
  }
  const uint32_t autosaveMs = millis();
  if (gDocumentDirty && !writeAutosaveNow()) {
    Serial.println("[ota] warning: autosave failed; continuing with update");
  }
  Serial.printf("[ota] autosave prep %lu ms\n",
                static_cast<unsigned long>(millis() - autosaveMs));
  gUndo.clear();
  gUi.setUndoRedoAvailable(false, false);

  // Enter the OTA overlay state before repainting the top bar, so any button
  // press cancelled by beginOta() is not drawn as still held.
  gUi.beginOta();

  // Splash and boot recovery have no canvas layer to preserve. Keep their
  // screen visible under the overlay so a failed update does not discard the
  // recovery prompt.
  const paint::Screen current = gUi.screen();
  if (current != paint::Screen::Splash &&
      current != paint::Screen::Recovery) {
    showCanvasFromBase();
  }
  drawOtaPanel();
  Serial.printf("[ota] overlay ready in %lu ms, heap=%u bytes\n",
                static_cast<unsigned long>(millis() - startMs),
                static_cast<unsigned>(rp2040.getFreeHeap()));
}

void onOtaProgress(void*, uint8_t percent) {
  if (gUi.setOtaProgress(percent)) {
    drawOtaPanel();
  }
}

void onOtaEnd(void*) {
  Serial.println("[ota] update complete; rebooting");
  if (gUi.setOtaProgress(100)) {
    drawOtaPanel();
  }
}

void onOtaError(void*, paint::OtaError error) {
  // This callback may run in the TCP/IP stack context (authentication errors
  // are reported directly from the UDP receive handler). Do not touch the
  // display or UI state here; the main loop consumes the request below.
  // ArduinoOTA can report a follow-up END error after CONNECT/RECEIVE; keep
  // the first, more specific error.
  if (!gOtaErrorPending) {
    gOtaErrorCode = error;
    gOtaErrorPending = true;
  }
}

void handleOtaErrorUi(paint::OtaError error, uint32_t now) {
  const char* message = "OTA failed";
  switch (error) {
    case paint::OtaError::Auth:
      message = "OTA auth failed";
      break;
    case paint::OtaError::Begin:
      message = "OTA begin failed";
      break;
    case paint::OtaError::Connect:
      message = "OTA connect failed";
      break;
    case paint::OtaError::Receive:
      message = "OTA receive failed";
      break;
    case paint::OtaError::End:
      message = "OTA end failed";
      break;
    case paint::OtaError::None:
      message = "OTA failed";
      break;
  }
  Serial.printf("[ota] %s\n", message);

  // A failed transfer can leave a partial staging file behind. Refresh the
  // gauge/free-space value before returning to the canvas so a later attempt is
  // gated correctly.
  if (gFilesystemReady) {
    updateStorageStats();
  }
  const paint::Screen current = gUi.screen();
  gUi.endOta();
  if (current == paint::Screen::Splash ||
      current == paint::Screen::Recovery) {
    // The overlay was drawn over the boot screen; keep that prompt intact.
    // On Splash the toast becomes visible when the splash completes.
    gUi.notify(message, now);
    gUi.compose(gFramebuffer, kFbWidth, kScreenRect);
    flushDisplayRect(kScreenRect);
    gUi.clearDirty();
    return;
  }

  showCanvasFromBase();
  gUi.notify(message, now);
  presentOverlayRect(paint::ui_layout::kToast);
  gUi.clearDirty();
}

bool mountFilesystem() {
  if (gStorage.begin()) {
    return true;
  }

  Serial.println("[fs] mount failed; formatting LittleFS once");
  if (!gStorage.format()) {
    Serial.println("[fs] format failed");
    return false;
  }
  if (!gStorage.begin()) {
    Serial.println("[fs] mount failed after format");
    return false;
  }
  return true;
}

void printFilesystemStats() {
  const paint::StorageStats stats = gStorage.stats();
  if (stats.totalBytes == 0) {
    Serial.println("[fs] info unavailable");
    return;
  }
  Serial.printf("[fs] total=%lu used=%lu free=%lu bytes\n",
                static_cast<unsigned long>(stats.totalBytes),
                static_cast<unsigned long>(stats.usedBytes),
                static_cast<unsigned long>(stats.freeBytes));
  Serial.printf("[fs] OTA reserve %s (need %lu bytes)\n",
                stats.otaHeadroom() ? "OK" : "LOW",
                static_cast<unsigned long>(paint::kOtaHeadroomBytes));
}

#ifdef PAINT_DIAG
void flushScreen() {
  gDirty.add(kScreenRect);
  gDisplay.flush(gDirty.take(), gFramebuffer, kFbWidth);
}

void runBenchmarks() {
  paint::gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect, kScreenRect, paint::kBlack);

  uint32_t start = micros();
  gDisplay.fillScreen(paint::kBlack);
  uint32_t elapsed = micros() - start;
  Serial.printf("[bench] fillScreen 320x480: %lu us\n", static_cast<unsigned long>(elapsed));

  const paint::Rect rect = {64, 64, 64, 64};
  paint::gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect, rect, paint::kRed);
  start = micros();
  gDisplay.flush(rect, gFramebuffer, kFbWidth);
  elapsed = micros() - start;
  Serial.printf("[bench] 64x64 rect flush: %lu us\n", static_cast<unsigned long>(elapsed));

  paint::gfx::drawLine(gFramebuffer, kFbWidth, kScreenRect, {20, 432}, {300, 432}, paint::kGreen);
  const paint::Rect lineBox = {20, 432, 281, 1};
  start = micros();
  gDisplay.flush(lineBox, gFramebuffer, kFbWidth);
  elapsed = micros() - start;
  Serial.printf("[bench] 432px line flush: %lu us\n", static_cast<unsigned long>(elapsed));

  start = micros();
  flushScreen();
  elapsed = micros() - start;
  Serial.printf("[bench] full-screen flush: %lu us\n", static_cast<unsigned long>(elapsed));
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);

  const uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart) < 2000) {
    delay(10);
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  printBanner();

  // Settings defaults must match the panel's native portrait orientation even
  // when /config.bin does not exist or the filesystem cannot be mounted.
  gSettings.rotation = paint::config::kDisplayRotation;
  gSettings.brightness = paint::config::kDefaultBrightness;
  gSettings.otaEnabled = true;

  if (mountFilesystem()) {
    gFilesystemReady = true;
    printFilesystemStats();
    gSettingsLoaded = paint::loadSettings(gSettings);
    if (gSettingsLoaded) {
      normalizeSettings();
    }
    Serial.printf("[cfg] %s\n",
                  gSettingsLoaded ? "loaded /config.bin" : "no saved settings");

    // Count this boot before any long-running subsystem starts. A normal boot
    // is marked healthy in the main loop after kBootHealthyMs.
    const paint::boot_health::State boot = paint::boot_health::begin();
    gSafeMode = boot.recoveryRequired();
    Serial.printf("[boot] health count=%u%s\n",
                  static_cast<unsigned>(boot.badBoots),
                  gSafeMode ? " (recovery mode)" : "");
  } else {
    Serial.println("[fs] unavailable; saving will be disabled");
  }

  Serial.println("[disp] initialising ILI9488");
  gDisplayOk = gDisplay.begin();
  if (!gDisplayOk) {
    Serial.println("[disp] init failed");
  }
  if (gSettingsLoaded) {
    gDisplay.setRotation(gSettings.rotation);
    gDisplay.setBacklight(gSettings.brightness);
  }
  Serial.printf("[disp] %dx%d, rotation %u, SPI %lu Hz\n",
                gDisplay.width(), gDisplay.height(),
                static_cast<unsigned>(gDisplay.rotation()),
                static_cast<unsigned long>(paint::config::kDisplaySpiHz));

  gFramebuffer = new (std::nothrow) uint16_t[kFbWidth * kFbHeight];
  if (!gFramebuffer) {
    Serial.println("[fb] allocation failed");
    gDisplay.fillScreen(paint::kRed);
    return;
  }
  Serial.printf("[fb] %dx%d RGB565 = %lu bytes, free heap %lu\n",
                kFbWidth, kFbHeight,
                static_cast<unsigned long>(sizeof(uint16_t) * kFbWidth * kFbHeight),
                static_cast<unsigned long>(rp2040.getFreeHeap()));

  gOverlayScratch = new (std::nothrow) uint16_t[kFbWidth * kOverlayBandHeight];
  if (!gOverlayScratch) {
    Serial.println("[ui] overlay scratch allocation failed");
    gDisplay.fillScreen(paint::kRed);
    return;
  }
  Serial.printf("[ui] overlay scratch %dx%d = %lu bytes, free heap %lu\n",
                kFbWidth, kOverlayBandHeight,
                static_cast<unsigned long>(sizeof(uint16_t) * kFbWidth *
                                           kOverlayBandHeight),
                static_cast<unsigned long>(rp2040.getFreeHeap()));

  gCanvas.setTarget(gFramebuffer, kFbWidth, paint::ui_layout::kCanvas);
  gStroke.setCanvas(&gCanvas);
  gStroke.setUndo(&gUndo);

  Serial.println("[touch] initialising HR2046/XPT2046 on SPI1");
  const bool touchCalibrated =
      gSettingsLoaded && paint::isCalibrationPlausible(gSettings.calibration);
  gTouchOk = gTouch.begin();
  if (!gTouchOk) {
    Serial.println("[touch] init failed");
  } else {
    Serial.printf("[touch] ready, SPI1 %lu Hz\n",
                  static_cast<unsigned long>(paint::config::kTouchSpiHz));
    if (touchCalibrated) {
      gTouch.setCalibration(gSettings.calibration);
      Serial.println("[cfg] touch calibration applied");
    } else if (gSettingsLoaded) {
      Serial.println("[cfg] saved settings have no valid calibration");
    }

    // Recovery entry point: a touch held through power-on chooses safe mode.
    // Require three positive samples to reject a noisy first conversion.
    int held = 0;
    for (int i = 0; i < 5; ++i) {
      paint::TouchPoint bootPoint;
      if (gTouch.read(bootPoint) && bootPoint.pressed) {
        ++held;
      }
      delay(10);
    }
    if (held >= 3) {
      gSafeMode = true;
      Serial.println("[boot] touch held: recovery mode");
    }
    gTouchState.reset();
  }

  gNetworkSuppressed = gSafeMode;
  gAutosaveSuppressed = gSafeMode;

#ifdef PAINT_DIAG
  runBenchmarks();
#endif

  gUi.begin(millis());
  gUi.setTouchCalibrated(touchCalibrated);
  gUi.setRotation(gSettings.rotation);
  gUi.setBrightness(gSettings.brightness);
  gUi.setSafeMode(gSafeMode, gDisplayOk, gTouchOk, gFilesystemReady);
  gUi.setThumbnailRenderer(renderPntThumbnail, nullptr);
  gUi.setDocument("", false);
  refreshGallery();
  updateStorageStats();

  if (gSafeMode) {
    Serial.println("[boot] Wi-Fi and autosave skipped in recovery mode");
  } else if (gFilesystemReady) {
    if (paint::isPntFileValid(paint::kAutosavePath)) {
      gUi.setRecoveryAvailable(true);
      Serial.println("[autosave] recovery prompt enabled");
    } else if (LittleFS.exists(paint::kAutosavePath)) {
      Serial.println("[autosave] invalid autosave removed");
      LittleFS.remove(paint::kAutosavePath);
      updateStorageStats();
    }
  }

  gUi.render(gFramebuffer, kFbWidth, kScreenRect);
  const paint::Rect initialDirty = gUi.takeDirty();
  const uint32_t start = micros();
  gDisplay.flush(initialDirty, gFramebuffer, kFbWidth);
  const uint32_t elapsed = micros() - start;
  Serial.printf("[disp] splash flush: %lu us\n", static_cast<unsigned long>(elapsed));

  Serial.printf("[mem] free heap before net: %u bytes\n",
                static_cast<unsigned>(rp2040.getFreeHeap()));
  gNet.setCallbacks(nullptr, onOtaStart, onOtaProgress, onOtaEnd, onOtaError);
  // In recovery, initialise the state as configured/disabled so the Wi-Fi
  // screen is accurate, but never start the radio. A normal boot applies the
  // persisted preference.
  gNet.begin(gSafeMode ? false : gSettings.otaEnabled, gStorageFreeBytes);
  Serial.printf("[mem] free heap: %u bytes\n",
                static_cast<unsigned>(rp2040.getFreeHeap()));

  gBootMs = millis();
  gIdle.reset(gBootMs);
  applyBacklight();
  Serial.println("[boot] ready");
}

void loop() {
  static uint32_t lastBlink = 0;
  static uint32_t lastHeartbeat = 0;
  static uint32_t lastTouchPoll = 0;
#ifdef PAINT_DIAG
  static uint32_t lastTouchPrint = 0;
#endif
  static bool ledOn = false;

  const uint32_t now = millis();

  if (!gBootHealthClearTried && !gSafeMode && gFilesystemReady &&
      now - gBootMs >= paint::config::kBootHealthyMs) {
    gBootHealthClearTried = true;
    gBootHealthCleared = paint::boot_health::clear();
    Serial.printf("[boot] health counter %s\n",
                  gBootHealthCleared ? "cleared" : "clear failed");
  }

  const paint::IdleLevel idleBefore = gIdle.level();
  // Keep the panel awake while an OTA is in progress or the user is in a
  // self-test/calibration screen; dimming those would be confusing. Capture
  // the previous level first so an OTA that starts while the display is off
  // restores the backlight in this same loop.
  if (gUi.otaActive() || gUi.screen() == paint::Screen::Calibrate ||
      gUi.screen() == paint::Screen::SafeMode ||
      gUi.screen() == paint::Screen::DisplayTest ||
      gUi.screen() == paint::Screen::TouchTest) {
    gIdle.noteTouch(now);
  }

  gIdle.update(now);
  if (gIdle.level() != idleBefore) {
    applyBacklight();
  }

  // A recovery boot starts Net disabled. loop() is still safe to call; it only
  // refreshes the storage guard and leaves the radio off.
  gNet.loop(now, gStorageFreeBytes);
  if (gOtaErrorPending) {
    gOtaErrorPending = false;
    handleOtaErrorUi(gOtaErrorCode, now);
  }
  gUi.setNetworkInfo(gNet.info());

  if (now - lastBlink >= kLedBlinkMs) {
    lastBlink = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
  }

  if (now - lastHeartbeat >= kHeartbeatMs) {
    lastHeartbeat = now;
    Serial.printf("[hb] uptime=%lu s heap=%u bytes\n",
                  static_cast<unsigned long>(now / 1000),
                  static_cast<unsigned>(rp2040.getFreeHeap()));
  }

  paint::TouchFrame frame;

  if (now - lastTouchPoll >= paint::config::kTouchPollMs) {
    lastTouchPoll = now;
    paint::TouchRaw raw;
    paint::TouchPoint point;
    if (gTouch.read(point, raw)) {
      const paint::Point mapped =
          mapTouchForDisplay({point.x, point.y});
      point.x = mapped.x;
      point.y = mapped.y;
      frame = gTouchState.update(point, now);
      if (raw.pressed) {
        frame.rawX = raw.x;
        frame.rawY = raw.y;
      }

      if (frame.down) {
        Serial.printf("[touch] down x=%d y=%d\n", frame.x, frame.y);
      }
      if (frame.longPress) {
        Serial.printf("[touch] long-press x=%d y=%d\n", frame.x, frame.y);
      }
      if (frame.up) {
        Serial.printf("[touch] up x=%d y=%d duration=%lu ms tap=%d\n",
                      frame.x, frame.y,
                      static_cast<unsigned long>(frame.durationMs),
                      frame.tap ? 1 : 0);
      }
      if (frame.tap) {
        Serial.printf("[touch] tap x=%d y=%d\n", frame.x, frame.y);
      }

#ifdef PAINT_DIAG
      static uint32_t lastMovePrint = 0;
      if (frame.move && now - lastMovePrint >= 100) {
        lastMovePrint = now;
        Serial.printf("[touch] move x=%d y=%d\n", frame.x, frame.y);
      }

      const uint32_t interval = frame.pressed ? 250 : 1000;
      if (now - lastTouchPrint >= interval) {
        lastTouchPrint = now;
        Serial.printf("[touch] raw=%u,%u z=%u mapped=%d,%d pressed=%d\n",
                      static_cast<unsigned>(raw.x),
                      static_cast<unsigned>(raw.y),
                      static_cast<unsigned>(raw.z),
                      static_cast<int>(frame.x),
                      static_cast<int>(frame.y),
                      raw.pressed ? 1 : 0);
      }
#endif
    }
  }

  const paint::IdleLevel idleAtTouch = gIdle.level();
  if (frame.pressed || frame.down || frame.move || frame.up) {
    gIdle.noteTouch(now);
    if (idleAtTouch == paint::IdleLevel::Off) {
      // The first press while the display is off only wakes it. Swallow the
      // whole gesture so the release cannot activate a hidden control.
      gWakeConsume = true;
    }
    if (gIdle.level() != idleAtTouch) {
      applyBacklight();
    }
  }
  if (gWakeConsume) {
    if (frame.up) {
      gWakeConsume = false;
    }
    frame = paint::TouchFrame{};
  }

  const paint::Screen screenBefore = gUi.screen();
  const bool canDraw = gBaseReady && screenBefore == paint::Screen::Canvas &&
                       gUi.sheet() == paint::Sheet::None;

  gUi.update(frame, now);

  paint::Calibration newCalibration;
  if (gUi.takeCalibration(newCalibration)) {
    gTouch.setCalibration(newCalibration);
    gSettings.calibration = newCalibration;
    const bool saved = paint::saveSettings(gSettings);
    gSettingsLoaded = gSettingsLoaded || saved;
    gUi.calibrationFinished(saved, now);
    if (saved) {
      updateStorageStats();
    }
    Serial.printf("[cal] a=%ld b=%ld c=%ld d=%ld e=%ld f=%ld saved=%d\n",
                  static_cast<long>(newCalibration.a),
                  static_cast<long>(newCalibration.b),
                  static_cast<long>(newCalibration.c),
                  static_cast<long>(newCalibration.d),
                  static_cast<long>(newCalibration.e),
                  static_cast<long>(newCalibration.f), saved ? 1 : 0);
  }

  if (gUi.takeRotationToggleRequest()) {
    gSettings.rotation =
        gSettings.rotation == 0 ? paint::config::kDisplayRotation : 0;
    gDisplay.setRotation(gSettings.rotation);
    gUi.setRotation(gSettings.rotation);
    const bool saved = saveAndApplySettings();
    gUi.notify(saved ? "Rotation flipped" : "Rotation unsaved", now);
    if (gUi.screen() == paint::Screen::Canvas) {
      // The canvas layer already holds the pixels; flushing the framebuffer is
      // enough and avoids repainting them with paper colour.
      flushDisplayRect(kScreenRect);
    } else {
      // Placeholder screens draw entirely from UI state, so recompose them in
      // the new orientation instead of flushing a stale framebuffer.
      gUi.compose(gFramebuffer, kFbWidth, kScreenRect);
      flushDisplayRect(kScreenRect);
      gUi.clearDirty();
    }
    Serial.printf("[disp] rotation %u%s\n",
                  static_cast<unsigned>(gSettings.rotation),
                  saved ? "" : " (not saved)");
  }

  int8_t brightnessStep = 0;
  if (gUi.takeBrightnessStepRequest(brightnessStep)) {
    const uint8_t current = brightnessIndex(gSettings.brightness);
    int next = static_cast<int>(current) + brightnessStep;
    if (next < 0) {
      next = 0;
    } else if (next >= static_cast<int>(paint::config::kBrightnessLevelCount)) {
      next = static_cast<int>(paint::config::kBrightnessLevelCount) - 1;
    }
    gSettings.brightness = paint::config::kBrightnessLevels[next];
    gUi.setBrightness(gSettings.brightness);
    const bool saved = saveAndApplySettings();
    applyBacklight();
    char message[40];
    std::snprintf(message, sizeof(message), "Brightness %u%%",
                  static_cast<unsigned>((gSettings.brightness * 100u) / 255u));
    gUi.notify(message, now);
    Serial.printf("[disp] brightness %u%s\n",
                  static_cast<unsigned>(gSettings.brightness),
                  saved ? "" : " (not saved)");
  }

  if (gUi.takeNetworkToggleRequest()) {
    const bool enable = !gNet.enabled();
    if (gNetworkSuppressed && enable) {
      gUi.notify("Restart to enable Wi-Fi", now);
    } else if (enable && !gNet.configured()) {
      gUi.notify("Wi-Fi not configured", now);
    } else if (enable && gStorageFreeBytes < paint::kOtaHeadroomBytes) {
      gUi.notify("Need 700 KB free for OTA", now);
    } else {
      gSettings.otaEnabled = enable;
      const bool saved = paint::saveSettings(gSettings);
      gSettingsLoaded = gSettingsLoaded || saved;
      if (!saved && gFilesystemReady) {
        Serial.println("[cfg] failed to persist Wi-Fi & OTA toggle");
      }
      gNet.setEnabled(enable, gStorageFreeBytes);
      gUi.setNetworkInfo(gNet.info());
      gUi.notify(enable ? "Wi-Fi & OTA enabled" : "Wi-Fi & OTA disabled", now);
      Serial.printf("[net] user %s Wi-Fi & OTA\n", enable ? "enabled" : "disabled");
    }
  }

  if (gUi.takeSafeModeContinueRequest()) {
    if (gFilesystemReady) {
      paint::boot_health::clear();
    }
    gBootHealthClearTried = true;
    gBootHealthCleared = true;
    gSafeMode = false;
    gUi.setSafeMode(false, gDisplayOk, gTouchOk, gFilesystemReady);
    showCanvasFromBase();
    gUi.notify("Offline; autosave off", now);
    Serial.println("[boot] continuing from recovery mode");
  }

  if (gUi.takeSafeModeRebootRequest()) {
    if (gFilesystemReady) {
      paint::boot_health::clear();
    }
    gBootHealthClearTried = true;
    Serial.println("[boot] restart requested from recovery mode");
    delay(50);
    rp2040.reboot();
  }

  if (gUi.screen() == paint::Screen::Splash) {
    if (gUi.dirty()) {
      gUi.compose(gFramebuffer, kFbWidth, kScreenRect);
      flushDisplayRect(kScreenRect);
      gUi.clearDirty();
    }
    delay(1);
    return;
  }

  if (!gSplashHandled) {
    gSplashHandled = true;
    if (!gSafeMode && gUi.screen() == paint::Screen::Canvas) {
      if (!gFilesystemReady) {
        gUi.notify("Storage unavailable", now);
      } else if (!gUi.hasOtaHeadroom()) {
        gUi.notify("OTA reserve low", now);
      } else if (!gDisplayOk) {
        gUi.notify("Display init failed", now);
      }
    }
  }

  if (!gBaseReady) {
    initializeCanvasBase();
  }

  if (screenBefore != paint::Screen::Gallery &&
      gUi.screen() == paint::Screen::Gallery) {
    refreshGallery();
  }

  if ((screenBefore != paint::Screen::Settings &&
       gUi.screen() == paint::Screen::Settings) ||
      (screenBefore != paint::Screen::Storage &&
       gUi.screen() == paint::Screen::Storage) ||
      (screenBefore != paint::Screen::Network &&
       gUi.screen() == paint::Screen::Network)) {
    updateStorageStats();
  }

  if (gUi.takeRecoveryRestoreRequest()) {
    cancelShape();
    if (gStroke.active()) {
      gStroke.cancel();
      gUndo.commit();
    }

    paint::DocumentInfo info;
    if (gFilesystemReady &&
        paint::loadPnt(paint::kAutosavePath, canvasPixels(), gCanvas.stride(),
                       static_cast<uint16_t>(gCanvas.bounds().w),
                       static_cast<uint16_t>(gCanvas.bounds().h), info)) {
      gUndo.clear();
      gCanvas.markDirty(gCanvas.bounds());
      gUi.setUndoRedoAvailable(false, false);
      setDocumentState("", true);
      rememberLastOpened("");
      gUi.showCanvas();
      gUi.notify("Recovered unsaved drawing", now);
      Serial.println("[autosave] recovered");
    } else {
      // Keep the autosave: a transient read error should still be recoverable
      // on the next boot.
      gUndo.clear();
      gCanvas.clear(paint::config::kPaperColor);
      gUi.setUndoRedoAvailable(false, false);
      setDocumentState("", false);
      rememberLastOpened("");
      gUi.showCanvas();
      gUi.notify("Recovery failed", now);
      Serial.println("[autosave] recovery failed");
    }
  }

  if (gUi.takeRecoveryDiscardRequest()) {
    clearAutosave();
    gUndo.clear();
    gCanvas.clear(paint::config::kPaperColor);
    gUi.setUndoRedoAvailable(false, false);
    setDocumentState("", false);
    rememberLastOpened("");
    gUi.showCanvas();
    gUi.notify("Autosave discarded", now);
    Serial.println("[autosave] discarded");
  }

  if (gUi.takeSaveAsRequest()) {
    if (!gFilesystemReady) {
      gUi.notify("Storage unavailable", now);
    } else {
      char name[paint::kPaintFileNameSize];
      if (gPaintFiles.nextName(name, sizeof(name))) {
        gUi.promptSaveAs(name);
      } else {
        gUi.notify("Gallery full (9999 files)", now);
      }
    }
  }

  char requestName[paint::kPaintFileNameSize];
  if (gUi.takeSaveRequest(requestName, sizeof(requestName))) {
    char path[32];
    if (!gFilesystemReady) {
      gUi.notify("Storage unavailable", now);
    } else if (!paint::paintFilePath(requestName, path, sizeof(path)) ||
               !paint::savePnt(path, canvasPixels(), gCanvas.stride(),
                               static_cast<uint16_t>(gCanvas.bounds().w),
                               static_cast<uint16_t>(gCanvas.bounds().h))) {
      gUi.notify("Save failed", now);
      Serial.printf("[save] failed: %s\n", requestName);
    } else {
      setDocumentState(requestName, false);
      clearAutosave();
      rememberLastOpened(requestName);
      refreshGallery();
      updateStorageStats();
      char message[48];
      if (gUi.hasOtaHeadroom()) {
        std::snprintf(message, sizeof(message), "Saved %s", requestName);
      } else {
        std::snprintf(message, sizeof(message), "Saved %s - OTA low", requestName);
      }
      gUi.notify(message, now);
      Serial.printf("[save] %s\n", path);
    }
  }

  if (gUi.takeLoadRequest(requestName, sizeof(requestName))) {
    char path[32];
    cancelShape();
    if (gStroke.active()) {
      gStroke.cancel();
      gUndo.commit();
    }

    paint::DocumentInfo info;
    if (!gFilesystemReady) {
      gUi.notify("Storage unavailable", now);
    } else if (!paint::paintFilePath(requestName, path, sizeof(path)) ||
               !paint::loadPnt(path, canvasPixels(), gCanvas.stride(),
                               static_cast<uint16_t>(gCanvas.bounds().w),
                               static_cast<uint16_t>(gCanvas.bounds().h), info)) {
      gUi.notify("Load failed", now);
      Serial.printf("[load] failed: %s\n", requestName);
    } else {
      gUndo.clear();
      gCanvas.markDirty(gCanvas.bounds());
      gUi.setUndoRedoAvailable(false, false);
      setDocumentState(requestName, false);
      clearAutosave();
      rememberLastOpened(requestName);
      gUi.showCanvas();
      char message[48];
      std::snprintf(message, sizeof(message), "Loaded %s", requestName);
      gUi.notify(message, now);
      Serial.printf("[load] %s\n", path);
    }
  }

  if (gUi.takeDeleteRequest(requestName, sizeof(requestName))) {
    char path[32];
    if (!gFilesystemReady || !paint::paintFilePath(requestName, path, sizeof(path)) ||
        !LittleFS.remove(path)) {
      gUi.notify("Delete failed", now);
      Serial.printf("[delete] failed: %s\n", requestName);
    } else {
      if (std::strcmp(gDocumentName, requestName) == 0) {
        setDocumentState("", true);
      }
      if (std::strcmp(gSettings.lastOpened, requestName) == 0) {
        rememberLastOpened("");
      }
      refreshGallery();
      updateStorageStats();
      char message[48];
      std::snprintf(message, sizeof(message), "Deleted %s", requestName);
      gUi.notify(message, now);
      Serial.printf("[delete] %s\n", path);
    }
  }

  if (gUi.takeUndoRequest()) {
    cancelShape();
    if (gStroke.active()) {
      gStroke.cancel();
      gUndo.commit();
    }
    if (gUndo.undo(gCanvas)) {
      markDocumentDirty();
      gUi.setUndoRedoAvailable(gUndo.canUndo(), gUndo.canRedo());
#ifdef PAINT_DIAG
      printUndoUsage("undo");
#endif
    }
  }

  if (gUi.takeRedoRequest()) {
    cancelShape();
    if (gStroke.active()) {
      gStroke.cancel();
      gUndo.commit();
    }
    if (gUndo.redo(gCanvas)) {
      markDocumentDirty();
      gUi.setUndoRedoAvailable(gUndo.canUndo(), gUndo.canRedo());
#ifdef PAINT_DIAG
      printUndoUsage("redo");
#endif
    }
  }

  if (gUi.takeNewCanvasRequest()) {
    cancelShape();
    gStroke.cancel();
    gUndo.clear();
    gCanvas.clear(paint::config::kPaperColor);
    gUi.setUndoRedoAvailable(false, false);
    setDocumentState("", false);
    clearAutosave();
    rememberLastOpened("");
    gUi.notify("New canvas", now);
#ifdef PAINT_DIAG
    printUndoUsage("new");
#endif
  }

  if (canDraw && (gUi.tool() == paint::Tool::Brush ||
                  gUi.tool() == paint::Tool::Eraser)) {
    const bool eraser = gUi.tool() == paint::Tool::Eraser;
    const paint::BrushMask& mask =
        paint::brushMaskForSize(gUi.brushSize());
    const uint16_t color =
        eraser ? paint::config::kPaperColor : gUi.color();

    if (frame.down && frame.y >= paint::config::kTopBarHeight) {
#ifdef PAINT_DIAG
      gStrokeDownUs = micros();
#endif
      gUndo.beginAction();
      gStroke.begin(mask, color, clampToCanvas({frame.x, frame.y}));
    } else if (gStroke.active() && frame.move) {
      gStroke.move(clampToCanvas({frame.x, frame.y}));
    } else if (gStroke.active() && frame.up) {
      gStroke.end(clampToCanvas({frame.x, frame.y}));
      gUndo.commit();
      markDocumentDirty();
#ifdef PAINT_DIAG
      printUndoUsage("commit");
#endif
      if (gUndo.takeOverflow()) {
        gUi.notify("Stroke too large to undo", now);
      }
      gUi.setUndoRedoAvailable(gUndo.canUndo(), gUndo.canRedo());
    }
  }

  if (canDraw && isShapeTool(gUi.tool())) {
    if (frame.down && frame.y >= paint::config::kTopBarHeight) {
      gShapeKind = shapeKindFor(gUi.tool());
      gShapeStart = clampToCanvas({frame.x, frame.y});
      gShape.update(gCanvas, gShapeKind, gShapeStart, gShapeStart, gUi.color(),
                    gUi.shapeFilled());
    } else if (gShape.active() && frame.move) {
      gShape.update(gCanvas, gShapeKind, gShapeStart,
                    clampToCanvas({frame.x, frame.y}), gUi.color(),
                    gUi.shapeFilled());
    } else if (gShape.active() && frame.up) {
      // Releasing over the toolbar cancels, matching the "drag off-canvas to
      // cancel" rule; a release inside the canvas clamps to the edge.
      if (!paint::ui_layout::kCanvas.contains(
              {static_cast<int16_t>(frame.x), static_cast<int16_t>(frame.y)})) {
        gShape.cancel(gCanvas);
      } else {
        gShape.commit(gCanvas, gUndo, gShapeKind, gShapeStart,
                      clampToCanvas({frame.x, frame.y}), gUi.color(),
                      gUi.shapeFilled());
        gUndo.commit();
        markDocumentDirty();
        finishUndoAction(now);
      }
    }
  }

  if (canDraw && gUi.tool() == paint::Tool::Fill && frame.down &&
      frame.y >= paint::config::kTopBarHeight) {
    gUndo.beginAction();
    const bool filled = paint::floodFill(
        gCanvas, &gUndo, clampToCanvas({frame.x, frame.y}), gUi.color());
    gUndo.commit();
    if (filled) {
      markDocumentDirty();
    }
    finishUndoAction(now);
    if (!filled) {
      gUi.notify("Fill area too complex", now);
    }
  }

  if (canDraw && gUi.tool() == paint::Tool::Picker && frame.down &&
      frame.y >= paint::config::kTopBarHeight) {
    const paint::Point p = clampToCanvas({frame.x, frame.y});
    gUi.pickColor(gCanvas.pixel(p.x, p.y), now);
  }

  if (gUi.screen() != paint::Screen::Canvas ||
      gUi.sheet() != paint::Sheet::None) {
    cancelShape();
    if (gStroke.active()) {
      gStroke.cancel();
      gUndo.commit();
      if (gUndo.takeOverflow()) {
        gUi.notify("Stroke too large to undo", now);
      }
      gUi.setUndoRedoAvailable(gUndo.canUndo(), gUndo.canRedo());
    }
  }

  if (!gAutosaveSuppressed) {
    autosaveIfDue(now);
  }

  if (gBaseReady) {
    flushCanvasDirty();
#ifdef PAINT_DIAG
    if (gStrokeDownUs != 0) {
      Serial.printf("[paint] first flush after %lu us\n",
                    static_cast<unsigned long>(micros() - gStrokeDownUs));
      gStrokeDownUs = 0;
    }
#endif
  }

  if (gUi.dirty()) {
    const int count = gUi.dirtyCount();
    for (int i = 0; i < count; ++i) {
      const paint::Rect dirty = gUi.dirtyRect(i);
      const uint32_t start = micros();
      presentDirtyRect(dirty);
      const uint32_t elapsed = micros() - start;
#ifdef PAINT_DIAG
      Serial.printf("[ui] present %d/%d %d,%d %dx%d: %lu us\n",
                    i + 1, count, dirty.x, dirty.y, dirty.w, dirty.h,
                    static_cast<unsigned long>(elapsed));
#endif
    }
    gUi.clearDirty();
  }

  delay(1);
}
