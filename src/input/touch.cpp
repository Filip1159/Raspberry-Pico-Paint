#include "input/touch.h"

#include <Arduino.h>
#include <SPI.h>

#include "config.h"

namespace paint {
namespace {

constexpr uint8_t kCmdZ1 = 0xB1;
constexpr uint8_t kCmdZ2 = 0xC1;
constexpr uint8_t kCmdX = 0x91;
constexpr uint8_t kCmdY = 0xD1;
constexpr uint8_t kCmdPowerDownY = 0xD0;
constexpr int32_t kPressureThreshold = 300;

uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) {
    const uint16_t t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    const uint16_t t = b;
    b = c;
    c = t;
  }
  if (a > b) {
    const uint16_t t = a;
    a = b;
    b = t;
  }
  return b;
}

int16_t mapRaw(uint16_t value, int16_t outMax) {
  int32_t v = value;
  if (v < 0) {
    v = 0;
  } else if (v > 4095) {
    v = 4095;
  }
  return static_cast<int16_t>((v * outMax + 2047) / 4095);
}

}  // namespace

bool Xpt2046Touch::begin() {
  pinMode(config::kTouchCs, OUTPUT);
  digitalWrite(config::kTouchCs, HIGH);
  pinMode(config::kTouchIrq, INPUT_PULLUP);

  SPI1.setSCK(config::kTouchSck);
  SPI1.setTX(config::kTouchMosi);
  SPI1.setRX(config::kTouchMiso);
  SPI1.begin();

  return true;
}

bool Xpt2046Touch::readRaw(TouchRaw& out) {
  out = TouchRaw{};

  uint16_t data[6] = {};
  int32_t pressure = 0;

  SPI1.beginTransaction(SPISettings(config::kTouchSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(config::kTouchCs, LOW);

  // XPT2046 conversions are pipelined: the command byte starts the next
  // conversion while the previous result is shifted out.
  SPI1.transfer(kCmdZ1);
  const uint16_t z1 = SPI1.transfer16(kCmdZ2) >> 3;
  pressure = static_cast<int32_t>(z1) + 4095;
  const uint16_t z2 = SPI1.transfer16(kCmdX) >> 3;
  pressure -= z2;

  if (pressure >= kPressureThreshold) {
    SPI1.transfer16(kCmdX);  // first X sample is noisy
    data[0] = SPI1.transfer16(kCmdY) >> 3;
    data[1] = SPI1.transfer16(kCmdX) >> 3;
    data[2] = SPI1.transfer16(kCmdY) >> 3;
    data[3] = SPI1.transfer16(kCmdX) >> 3;
  }

  data[4] = SPI1.transfer16(kCmdPowerDownY) >> 3;
  data[5] = SPI1.transfer16(0x0000) >> 3;

  digitalWrite(config::kTouchCs, HIGH);
  SPI1.endTransaction();

  if (pressure < 0) {
    pressure = 0;
  }
  out.z = static_cast<uint16_t>(pressure);

  if (pressure < kPressureThreshold) {
    return true;
  }

  const uint16_t x = median3(data[0], data[2], data[4]);
  const uint16_t y = median3(data[1], data[3], data[5]);
  if (x > 4095 || y > 4095) {
    return true;  // floating MISO or an invalid conversion
  }

  out.x = x;
  out.y = y;
  out.pressed = true;
  return true;
}

bool Xpt2046Touch::read(TouchPoint& out) {
  TouchRaw raw;
  if (!readRaw(raw)) {
    return false;
  }

  if (!raw.pressed) {
    out = TouchPoint{};
    wasPressed_ = false;
    return true;
  }

  const int16_t sx = mapRaw(raw.x, config::kScreenWidth - 1);
  const int16_t sy = mapRaw(raw.y, config::kScreenHeight - 1);

  if (!wasPressed_) {
    emaX_ = sx;
    emaY_ = sy;
    wasPressed_ = true;
  } else {
    emaX_ = static_cast<int16_t>((emaX_ * 3 + sx) / 4);
    emaY_ = static_cast<int16_t>((emaY_ * 3 + sy) / 4);
  }

  out.x = emaX_;
  out.y = emaY_;
  out.pressed = true;
  return true;
}

}  // namespace paint
