#include <Arduino.h>
#include <LittleFS.h>

#include "config.h"
#include "version.h"

namespace {

constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kLedBlinkMs = 500;

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
  Serial.println("  stage   : M1 skeleton");
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

}  // namespace

void setup() {
  Serial.begin(115200);

  // Give USB CDC a moment to enumerate, but never block boot indefinitely.
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

  Serial.printf("[mem] free heap: %u bytes\n",
                static_cast<unsigned>(rp2040.getFreeHeap()));
  Serial.println("[boot] ready");
}

void loop() {
  static uint32_t lastBlink = 0;
  static uint32_t lastHeartbeat = 0;
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

  delay(1);
}
