#pragma once

// Non-blocking Wi-Fi and ArduinoOTA lifecycle (implementation lands in M7).

#include <cstdint>

namespace paint {

enum class NetState : uint8_t {
  Offline,
  Connecting,
  Connected,
  OtaActive,
};

class Net {
 public:
  void begin();
  void loop();
  NetState state() const { return state_; }

 private:
  NetState state_ = NetState::Offline;
};

}  // namespace paint
