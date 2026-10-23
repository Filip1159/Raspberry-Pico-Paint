#include <Arduino.h>
#include <LittleFS.h>

#include <cstdio>
#include <new>

#include "config.h"
#include "display/dirty.h"
#include "display/gfx.h"
#include "display/ili9488.h"
#include "input/touch.h"
#include "paint/color.h"
#include "version.h"

namespace {

constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kLedBlinkMs = 500;
constexpr int kFbWidth = paint::config::kScreenWidth;
constexpr int kFbHeight = paint::config::kScreenHeight;

const paint::Rect kScreenRect = {0, 0, kFbWidth, kFbHeight};

paint::Ili9488 gDisplay;
paint::DirtyRegion gDirty;
paint::Xpt2046Touch gTouch;
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
  Serial.println("  stage   : M2 display bring-up");
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

void flushScreen() {
  gDirty.add(kScreenRect);
  gDisplay.flush(gDirty.take(), gFramebuffer, kFbWidth);
}

void drawDiagnostics() {
  using namespace paint;

  gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect, kScreenRect, kBlack);

  gfx::drawText(gFramebuffer, kFbWidth, kScreenRect, {8, 8}, "Pico Paint", kWhite, 3);
  gfx::drawText(gFramebuffer, kFbWidth, kScreenRect, {8, 40}, "M2 ILI9488 test", kCyan, 2);
  gfx::drawText(gFramebuffer, kFbWidth, kScreenRect, {8, 62}, "320x480 portrait", kWhite, 2);

  const uint16_t bars[8] = {kRed, kGreen, kBlue, kWhite, kBlack, kYellow, kCyan, kMagenta};
  for (int i = 0; i < 8; ++i) {
    gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect,
                  {static_cast<int16_t>(i * 40), 90, 40, 60}, bars[i]);
  }

  for (int i = 0; i < 16; ++i) {
    const uint8_t v = static_cast<uint8_t>(i * 17);
    gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect,
                  {static_cast<int16_t>(i * 20), 160, 20, 30}, rgb565(v, v, v));
  }

  for (int y = 0; y < 80; y += 8) {
    for (int x = 0; x < kFbWidth; x += 8) {
      if (((x / 8) + (y / 8)) & 1) {
        gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect,
                      {static_cast<int16_t>(x), static_cast<int16_t>(200 + y), 8, 8}, kWhite);
      }
    }
  }

  char spiText[24];
  std::snprintf(spiText, sizeof(spiText), "SPI %lu MHz",
                static_cast<unsigned long>(paint::config::kDisplaySpiHz / 1000000UL));
  gfx::drawText(gFramebuffer, kFbWidth, kScreenRect, {8, 292}, spiText, kYellow, 2);

  char heapText[32];
  std::snprintf(heapText, sizeof(heapText), "heap %lu",
                static_cast<unsigned long>(rp2040.getFreeHeap()));
  gfx::drawText(gFramebuffer, kFbWidth, kScreenRect, {8, 314}, heapText, kWhite, 2);
  gfx::drawText(gFramebuffer, kFbWidth, kScreenRect, {8, 336}, "touch HR2046", kWhite, 2);

  gfx::drawCircle(gFramebuffer, kFbWidth, kScreenRect, {60, 410}, 30, kMagenta);
  gfx::drawLine(gFramebuffer, kFbWidth, kScreenRect, {110, 455}, {300, 455}, kGreen);
  gfx::drawRect(gFramebuffer, kFbWidth, kScreenRect, {4, 4, 312, 472}, kBlue);
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

  drawDiagnostics();
  const uint32_t start = micros();
  flushScreen();
  const uint32_t elapsed = micros() - start;
  Serial.printf("[disp] diagnostics flush: %lu us\n", static_cast<unsigned long>(elapsed));

  Serial.printf("[mem] free heap: %u bytes\n",
                static_cast<unsigned>(rp2040.getFreeHeap()));
  Serial.println("[boot] ready");
}

void loop() {
  static uint32_t lastBlink = 0;
  static uint32_t lastHeartbeat = 0;
  static uint32_t lastTouchPoll = 0;
  static uint32_t lastTouchPrint = 0;
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

  if (now - lastTouchPoll >= 10) {
    lastTouchPoll = now;
    paint::TouchRaw raw;
    paint::TouchPoint point;
    if (gTouch.read(point, raw)) {
      const uint32_t interval = raw.pressed ? 100 : 1000;
      if (now - lastTouchPrint >= interval) {
        lastTouchPrint = now;
        Serial.printf("[touch] raw=%u,%u z=%u mapped=%d,%d pressed=%d\n",
                      static_cast<unsigned>(raw.x),
                      static_cast<unsigned>(raw.y),
                      static_cast<unsigned>(raw.z),
                      static_cast<int>(point.x),
                      static_cast<int>(point.y),
                      raw.pressed ? 1 : 0);
      }

      if (point.pressed) {
        // Temporary visual check until the M3 UI shell replaces the
        // diagnostics screen: a red crosshair follows the calibrated touch.
        const int16_t mx = point.x;
        const int16_t my = point.y;
        paint::gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect,
                             {static_cast<int16_t>(mx - 4), my, 9, 1}, paint::kRed);
        paint::gfx::fillRect(gFramebuffer, kFbWidth, kScreenRect,
                             {mx, static_cast<int16_t>(my - 4), 1, 9}, paint::kRed);
        gDisplay.flush({static_cast<int16_t>(mx - 4), static_cast<int16_t>(my - 4), 9, 9},
                       gFramebuffer, kFbWidth);
      }
    }
  }

  delay(1);
}
