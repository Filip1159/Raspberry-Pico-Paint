#include "display/ili9488.h"

#include <Arduino.h>
#include <SPI.h>

#include "config.h"
#include "paint/color.h"

namespace paint {
namespace {

constexpr uint8_t kCmdSoftReset = 0x01;
constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdNormalMode = 0x13;
constexpr uint8_t kCmdDisplayOn = 0x29;
constexpr uint8_t kCmdColumnAddress = 0x2A;
constexpr uint8_t kCmdPageAddress = 0x2B;
constexpr uint8_t kCmdMemoryWrite = 0x2C;
constexpr uint8_t kCmdMadctl = 0x36;
constexpr uint8_t kCmdPixelFormat = 0x3A;

}  // namespace

bool Ili9488::begin() {
  pinMode(config::kDisplayCs, OUTPUT);
  digitalWrite(config::kDisplayCs, HIGH);
  pinMode(config::kDisplayDc, OUTPUT);
  digitalWrite(config::kDisplayDc, HIGH);
  pinMode(config::kDisplayRst, OUTPUT);
  digitalWrite(config::kDisplayRst, HIGH);
  pinMode(config::kBacklightPin, OUTPUT);
  digitalWrite(config::kBacklightPin, LOW);

  // The display is write-only. Do not leave SPI0 RX on its default GP16,
  // because GP16 is used as the D/C pin. GP0 is an unused valid SPI0 RX pin.
  SPI.setSCK(config::kDisplaySck);
  SPI.setTX(config::kDisplayMosi);
  SPI.setRX(0);
  SPI.begin();
  spiHz_ = config::kDisplaySpiHz;

  reset();
  initSequence();
  setRotation(config::kDisplayRotation);

  analogWriteFreq(1000);
  analogWriteRange(255);
  analogWrite(config::kBacklightPin, 255);

  return true;
}

void Ili9488::setSpiClock(uint32_t hz) {
  spiHz_ = hz;
}

void Ili9488::setBacklight(uint8_t duty) {
  analogWrite(config::kBacklightPin, duty);
}

void Ili9488::reset() {
  digitalWrite(config::kDisplayRst, HIGH);
  delay(10);
  digitalWrite(config::kDisplayRst, LOW);
  delay(10);
  digitalWrite(config::kDisplayRst, HIGH);
  delay(120);
}

void Ili9488::initSequence() {
  writeCommand(kCmdSoftReset);
  delay(120);

  writeCommand(0xE0);  // Positive gamma
  writeData8(0x00);
  writeData8(0x03);
  writeData8(0x09);
  writeData8(0x08);
  writeData8(0x16);
  writeData8(0x0A);
  writeData8(0x3F);
  writeData8(0x78);
  writeData8(0x4C);
  writeData8(0x09);
  writeData8(0x0A);
  writeData8(0x08);
  writeData8(0x16);
  writeData8(0x1A);
  writeData8(0x0F);

  writeCommand(0xE1);  // Negative gamma
  writeData8(0x00);
  writeData8(0x16);
  writeData8(0x19);
  writeData8(0x03);
  writeData8(0x0F);
  writeData8(0x05);
  writeData8(0x32);
  writeData8(0x45);
  writeData8(0x46);
  writeData8(0x04);
  writeData8(0x0E);
  writeData8(0x0D);
  writeData8(0x35);
  writeData8(0x37);
  writeData8(0x0F);

  writeCommand(0xC0);  // Power control 1
  writeData8(0x17);
  writeData8(0x15);

  writeCommand(0xC1);  // Power control 2
  writeData8(0x41);

  writeCommand(0xC5);  // VCOM control
  writeData8(0x00);
  writeData8(0x12);
  writeData8(0x80);

  writeCommand(kCmdMadctl);  // Memory access control: MX + BGR
  writeData8(0x48);

  writeCommand(kCmdPixelFormat);
  writeData8(0x66);  // 18-bit RGB666 for SPI

  writeCommand(0xB0);  // Interface mode control
  writeData8(0x00);

  writeCommand(0xB1);  // Frame rate control
  writeData8(0xA0);

  writeCommand(0xB4);  // Display inversion control
  writeData8(0x02);

  writeCommand(0xB6);  // Display function control
  writeData8(0x02);
  writeData8(0x02);
  writeData8(0x3B);

  writeCommand(0xB7);  // Entry mode set
  writeData8(0xC6);

  writeCommand(0xF7);  // Adjust control 3
  writeData8(0xA9);
  writeData8(0x51);
  writeData8(0x2C);
  writeData8(0x82);

  writeCommand(kCmdSleepOut);
  delay(120);

  writeCommand(kCmdNormalMode);
  writeCommand(kCmdDisplayOn);
  delay(25);
}

void Ili9488::setRotation(uint8_t rotation) {
  uint8_t madctl = 0x48;

  switch (rotation & 0x03) {
    case 0:
      // Both address axes are reversed relative to rotation 2, giving a true
      // 180-degree portrait flip rather than a mirror image. Touch mapping in
      // input/orientation.h uses the same full flip.
      madctl = 0xC8;  // MX + MY + BGR
      width_ = 320;
      height_ = 480;
      break;
    case 1:
      madctl = 0x28;  // MV + BGR
      width_ = 480;
      height_ = 320;
      break;
    case 2:
      madctl = 0x88;  // MY + BGR
      width_ = 320;
      height_ = 480;
      break;
    default:
      madctl = 0xE8;  // MX + MY + MV + BGR
      width_ = 480;
      height_ = 320;
      break;
  }

  writeCommand(kCmdMadctl);
  writeData8(madctl);
  rotation_ = rotation & 0x03;
}

void Ili9488::writeCommand(uint8_t command) {
  digitalWrite(config::kDisplayCs, LOW);
  digitalWrite(config::kDisplayDc, LOW);
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
  SPI.transfer(command);
  SPI.endTransaction();
  digitalWrite(config::kDisplayCs, HIGH);
}

void Ili9488::writeData8(uint8_t data) {
  digitalWrite(config::kDisplayCs, LOW);
  digitalWrite(config::kDisplayDc, HIGH);
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
  SPI.transfer(data);
  SPI.endTransaction();
  digitalWrite(config::kDisplayCs, HIGH);
}

void Ili9488::writeData16(uint16_t data) {
  writeData8(static_cast<uint8_t>(data >> 8));
  writeData8(static_cast<uint8_t>(data & 0xFF));
}

void Ili9488::writeDataBuf(const uint8_t* data, size_t length) {
  if (!data || length == 0) {
    return;
  }
  digitalWrite(config::kDisplayCs, LOW);
  digitalWrite(config::kDisplayDc, HIGH);
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
  SPI.transfer(data, nullptr, length);
  SPI.endTransaction();
  digitalWrite(config::kDisplayCs, HIGH);
}

void Ili9488::setAddrWindow(int x, int y, int w, int h) {
  writeCommand(kCmdColumnAddress);
  writeData16(static_cast<uint16_t>(x));
  writeData16(static_cast<uint16_t>(x + w - 1));

  writeCommand(kCmdPageAddress);
  writeData16(static_cast<uint16_t>(y));
  writeData16(static_cast<uint16_t>(y + h - 1));

  writeCommand(kCmdMemoryWrite);
}

void Ili9488::fillLine(uint16_t color, int pixels) {
  const uint8_t r = static_cast<uint8_t>(red6(color) << 2);
  const uint8_t g = static_cast<uint8_t>(green6_from_565(color) << 2);
  const uint8_t b = static_cast<uint8_t>(blue6(color) << 2);

  uint8_t* dst = line_;
  for (int i = 0; i < pixels; ++i) {
    *dst++ = r;
    *dst++ = g;
    *dst++ = b;
  }
}

void Ili9488::fillScreen(uint16_t color) {
  setAddrWindow(0, 0, width_, height_);
  fillLine(color, width_);

  digitalWrite(config::kDisplayCs, LOW);
  digitalWrite(config::kDisplayDc, HIGH);
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
  for (int y = 0; y < height_; ++y) {
    SPI.transfer(line_, nullptr, static_cast<size_t>(width_ * 3));
    if ((y & 0x0F) == 0) {
      rp2040.wdt_reset();
    }
  }
  SPI.endTransaction();
  digitalWrite(config::kDisplayCs, HIGH);
}

void Ili9488::flush(const Rect& rect, const uint16_t* pixels, int stride) {
  if (!pixels) {
    return;
  }

  const Rect r = rect.intersect({0, 0, static_cast<int16_t>(width_), static_cast<int16_t>(height_)});
  if (r.empty()) {
    return;
  }

  setAddrWindow(r.x, r.y, r.w, r.h);

  digitalWrite(config::kDisplayCs, LOW);
  digitalWrite(config::kDisplayDc, HIGH);
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));

  for (int y = 0; y < r.h; ++y) {
    const uint16_t* src = pixels + static_cast<size_t>(r.y + y) * stride + r.x;
    uint8_t* dst = line_;
    for (int x = 0; x < r.w; ++x) {
      const uint16_t c = src[x];
      *dst++ = static_cast<uint8_t>(red6(c) << 2);
      *dst++ = static_cast<uint8_t>(green6_from_565(c) << 2);
      *dst++ = static_cast<uint8_t>(blue6(c) << 2);
    }
    SPI.transfer(line_, nullptr, static_cast<size_t>(r.w * 3));
    if ((y & 0x0F) == 0) {
      rp2040.wdt_reset();
    }
  }

  SPI.endTransaction();
  digitalWrite(config::kDisplayCs, HIGH);
}

}  // namespace paint
