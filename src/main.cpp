#include <Arduino.h>
#include <LittleFS.h>

#include <cstdio>
#include <new>

#include "config.h"
#include "display/dirty.h"
#include "display/gfx.h"
#include "display/ili9488.h"
#include "input/touch.h"
#include "input/touch_state.h"
#include "paint/color.h"
#include "ui/ui.h"
#include "version.h"

namespace {

constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kLedBlinkMs = 500;
constexpr int kFbWidth = paint::config::kScreenWidth;
constexpr int kFbHeight = paint::config::kScreenHeight;

const paint::Rect kScreenRect = {0, 0, kFbWidth, kFbHeight};

paint::Ili9488 gDisplay;
#ifdef PAINT_DIAG
paint::DirtyRegion gDirty;
#endif
paint::Xpt2046Touch gTouch;
paint::TouchStateMachine gTouchState;
paint::Ui gUi;
uint16_t* gFramebuffer = nullptr;

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
  Serial.println("  stage   : M3 touch + UI shell");
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

  Serial.println("[touch] initialising HR2046/XPT2046 on SPI1");
  if (!gTouch.begin()) {
    Serial.println("[touch] init failed");
  } else {
    Serial.printf("[touch] ready, SPI1 %lu Hz\n",
                  static_cast<unsigned long>(paint::config::kTouchSpiHz));
  }

#ifdef PAINT_DIAG
  runBenchmarks();
#endif

  gUi.begin(millis());
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
      if (frame.move) {
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

  gUi.update(frame, now);
  gUi.render(gFramebuffer, kFbWidth, kScreenRect);

  if (gUi.dirty()) {
    const int count = gUi.dirtyCount();
    for (int i = 0; i < count; ++i) {
      const paint::Rect dirty = gUi.dirtyRect(i);
      const uint32_t start = micros();
      gDisplay.flush(dirty, gFramebuffer, kFbWidth);
      const uint32_t elapsed = micros() - start;
#ifdef PAINT_DIAG
      Serial.printf("[ui] flush %d/%d %d,%d %dx%d: %lu us\n",
                    i + 1, count, dirty.x, dirty.y, dirty.w, dirty.h,
                    static_cast<unsigned long>(elapsed));
#endif
    }
    gUi.clearDirty();
  }

  delay(1);
}
