#pragma once

// Immediate-mode UI shell (implementation lands in M3).

#include <cstdint>

namespace paint {

enum class Screen : uint8_t {
  Splash,
  Canvas,
  Gallery,
  Settings,
  About,
  Recovery,
};

class Ui {
 public:
  Screen screen() const { return screen_; }
  void setScreen(Screen screen) { screen_ = screen; }

 private:
  Screen screen_ = Screen::Splash;
};

}  // namespace paint
