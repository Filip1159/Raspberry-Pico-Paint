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
#include "input/touch.h"
#include "input/touch_state.h"
#include "paint/brush.h"
#include "paint/canvas.h"
#include "paint/color.h"
#include "paint/shapes.h"
#include "paint/stroke.h"
#include "paint/undo.h"
#include "storage/settings.h"
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
  int x = p.x;
  int y = p.y;
  if (x < paint::ui_layout::kCanvas.x) {
    x = paint::ui_layout::kCanvas.x;
  }
  if (x >= paint::ui_layout::kCanvas.right()) {
    x = paint::ui_layout::kCanvas.right() - 1;
  }
  if (y < paint::ui_layout::kCanvas.y) {
    y = paint::ui_layout::kCanvas.y;
  }
  if (y >= paint::ui_layout::kCanvas.bottom()) {
    y = paint::ui_layout::kCanvas.bottom() - 1;
  }
  return paint::Point{static_cast<int16_t>(x), static_cast<int16_t>(y)};
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
  Serial.println("  stage   : M4 drawing + compressed undo");
  Serial.println("========================================");
}

bool mountFilesystem() {
  if (LittleFS.begin()) {
    return true;
  }

  Serial.println("[fs] mount failed; formatting LittleFS once");
  if (!LittleFS.format()) {
    Serial.println("[fs] format failed");
    return false;
  }
  if (!LittleFS.begin()) {
    Serial.println("[fs] mount failed after format");
    return false;
  }
  return true;
}

void printFilesystemStats() {
  FSInfo info;
  if (!LittleFS.info(info)) {
    Serial.println("[fs] info unavailable");
    return;
  }
  Serial.printf("[fs] total=%lu used=%lu free=%lu bytes\n",
                static_cast<unsigned long>(info.totalBytes),
                static_cast<unsigned long>(info.usedBytes),
                static_cast<unsigned long>(info.totalBytes - info.usedBytes));
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

  if (mountFilesystem()) {
    printFilesystemStats();
    gSettingsLoaded = paint::loadSettings(gSettings);
    Serial.printf("[cfg] %s\n",
                  gSettingsLoaded ? "loaded /config.bin" : "no saved settings");
  } else {
    Serial.println("[fs] unavailable; saving will be disabled");
  }

  Serial.println("[disp] initialising ILI9488");
  if (!gDisplay.begin()) {
    Serial.println("[disp] init failed");
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
  if (!gTouch.begin()) {
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
  }

#ifdef PAINT_DIAG
  runBenchmarks();
#endif

  gUi.begin(millis());
  gUi.setTouchCalibrated(touchCalibrated);
  gUi.render(gFramebuffer, kFbWidth, kScreenRect);
  const paint::Rect initialDirty = gUi.takeDirty();
  const uint32_t start = micros();
  gDisplay.flush(initialDirty, gFramebuffer, kFbWidth);
  const uint32_t elapsed = micros() - start;
  Serial.printf("[disp] splash flush: %lu us\n", static_cast<unsigned long>(elapsed));

  Serial.printf("[mem] free heap: %u bytes\n",
                static_cast<unsigned>(rp2040.getFreeHeap()));
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
    Serial.printf("[cal] a=%ld b=%ld c=%ld d=%ld e=%ld f=%ld saved=%d\n",
                  static_cast<long>(newCalibration.a),
                  static_cast<long>(newCalibration.b),
                  static_cast<long>(newCalibration.c),
                  static_cast<long>(newCalibration.d),
                  static_cast<long>(newCalibration.e),
                  static_cast<long>(newCalibration.f), saved ? 1 : 0);
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

  if (!gBaseReady) {
    initializeCanvasBase();
  }

  if (gUi.takeUndoRequest()) {
    cancelShape();
    if (gStroke.active()) {
      gStroke.cancel();
      gUndo.commit();
    }
    if (gUndo.undo(gCanvas)) {
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
      gShape.update(gCanvas, gShapeKind, gShapeStart, gShapeStart);
    } else if (gShape.active() && frame.move) {
      gShape.update(gCanvas, gShapeKind, gShapeStart,
                    clampToCanvas({frame.x, frame.y}));
    } else if (gShape.active() && frame.up) {
      gShape.commit(gCanvas, gUndo, gShapeKind, gShapeStart,
                    clampToCanvas({frame.x, frame.y}), gUi.color());
      gUndo.commit();
      finishUndoAction(now);
    }
  }

  if (canDraw && gUi.tool() == paint::Tool::Fill && frame.down &&
      frame.y >= paint::config::kTopBarHeight) {
    gUndo.beginAction();
    const bool filled = paint::floodFill(
        gCanvas, &gUndo, clampToCanvas({frame.x, frame.y}), gUi.color());
    gUndo.commit();
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
