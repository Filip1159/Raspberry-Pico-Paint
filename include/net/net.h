#pragma once

// Non-blocking Wi-Fi and ArduinoOTA lifecycle. This header deliberately keeps
// the Arduino/WiFi types out of the interface so the native UI tests can use
// NetInfo without linking the wireless stack.

#include <cstddef>
#include <cstdint>

namespace paint {

enum class NetState : uint8_t {
  Disabled,
  Offline,
  Connecting,
  Connected,
  OtaActive,
};

enum class OtaError : uint8_t {
  None,
  Auth,
  Begin,
  Connect,
  Receive,
  End,
};

struct NetInfo {
  NetState state = NetState::Disabled;
  bool enabled = false;
  bool configured = false;
  bool server = false;
  bool lowStorage = false;
  uint8_t ip[4] = {};
  int32_t rssi = 0;
  uint8_t progress = 0;
  OtaError error = OtaError::None;

  bool hasIp() const {
    return (ip[0] | ip[1] | ip[2] | ip[3]) != 0;
  }
};

class Net {
 public:
  using StartCallback = void (*)(void* context);
  using ProgressCallback = void (*)(void* context, uint8_t percent);
  using EndCallback = void (*)(void* context);
  using ErrorCallback = void (*)(void* context, OtaError error);

  void setCallbacks(void* context, StartCallback start,
                    ProgressCallback progress, EndCallback end,
                    ErrorCallback error);

  // Start with the persisted preference. `freeBytes` is the LittleFS free
  // space used by the OTA staging guard.
  void begin(bool enabled, size_t freeBytes);
  void setEnabled(bool enabled, size_t freeBytes);

  // Call frequently. This only blocks while an OTA transfer is in progress;
  // ArduinoOTA drives that transfer synchronously and the callbacks repaint
  // the progress overlay directly.
  void loop(uint32_t nowMs, size_t freeBytes);

  NetInfo info() const;
  NetState state() const { return state_; }
  bool enabled() const { return enabled_; }
  bool configured() const { return configured_; }

 private:
  void registerCallbacks();
  void startConnection(uint32_t nowMs);
  void startServer();
  void stopServer();
  void handleDisconnect(uint32_t nowMs);
  void updateDetails(uint32_t nowMs);

  bool enabled_ = false;
  bool configured_ = false;
  bool server_ = false;
  bool lowStorage_ = false;
  bool callbacksRegistered_ = false;
  NetState state_ = NetState::Disabled;
  OtaError error_ = OtaError::None;
  uint8_t ip_[4] = {};
  int32_t rssi_ = 0;
  uint8_t progress_ = 0;
  size_t freeBytes_ = 0;
  uint32_t connectStarted_ = 0;
  uint32_t nextAttempt_ = 0;
  uint32_t nextDetails_ = 0;

  void* context_ = nullptr;
  StartCallback start_ = nullptr;
  ProgressCallback progressCallback_ = nullptr;
  EndCallback end_ = nullptr;
  ErrorCallback errorCallback_ = nullptr;
};

}  // namespace paint
