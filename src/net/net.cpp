#include "net/net.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include <cstring>

#include "config.h"
#include "storage/storage.h"

namespace paint {
namespace {

constexpr uint32_t kConnectTimeoutMs = 10'000;
constexpr uint32_t kRetryIntervalMs = 30'000;
constexpr uint32_t kDetailsIntervalMs = 1'000;

bool isStaticIpConfigured() {
  return config::kStaticIp && config::kStaticIp[0] != '\0' &&
         std::strcmp(config::kStaticIp, "0.0.0.0") != 0;
}

void applyStaticIp() {
  if (!isStaticIpConfigured()) {
    return;
  }

  IPAddress local;
  IPAddress gateway;
  IPAddress subnet;
  if (!local.fromString(config::kStaticIp) ||
      !gateway.fromString(config::kGateway) ||
      !subnet.fromString(config::kSubnet)) {
    Serial.println("[net] invalid static IP configuration; using DHCP");
    return;
  }

  // The Arduino WiFi API wants (local, dns, gateway, subnet); using the
  // gateway as DNS is adequate for OTA (no hostname resolution is required).
  WiFi.config(local, gateway, gateway, subnet);
  Serial.printf("[net] static IP %s\n", config::kStaticIp);
}

OtaError mapOtaError(ota_error_t error) {
  switch (error) {
    case OTA_AUTH_ERROR:
      return OtaError::Auth;
    case OTA_BEGIN_ERROR:
      return OtaError::Begin;
    case OTA_CONNECT_ERROR:
      return OtaError::Connect;
    case OTA_RECEIVE_ERROR:
      return OtaError::Receive;
    case OTA_END_ERROR:
      return OtaError::End;
    default:
      return OtaError::End;
  }
}

}  // namespace

void Net::setCallbacks(void* context, StartCallback start,
                       ProgressCallback progress, EndCallback end,
                       ErrorCallback error) {
  context_ = context;
  start_ = start;
  progressCallback_ = progress;
  end_ = end;
  errorCallback_ = error;
}

void Net::begin(bool enabled, size_t freeBytes) {
  enabled_ = enabled;
  freeBytes_ = freeBytes;
  lowStorage_ = freeBytes < kOtaHeadroomBytes;
  configured_ = config::kWifiSsid && config::kWifiSsid[0] != '\0';
  error_ = OtaError::None;
  progress_ = 0;
  server_ = false;

  if (!configured_) {
    state_ = NetState::Disabled;
    Serial.println("[net] Wi-Fi not configured; offline mode");
    return;
  }
  if (!enabled_) {
    state_ = NetState::Disabled;
    Serial.println("[net] Wi-Fi & OTA disabled");
    return;
  }
  if (freeBytes_ < kOtaHeadroomBytes) {
    state_ = NetState::Offline;
    lowStorage_ = true;
    nextAttempt_ = millis() + kRetryIntervalMs;
    Serial.println("[net] OTA reserve low; Wi-Fi connection deferred");
    return;
  }

  startConnection(millis());
}

void Net::setEnabled(bool enabled, size_t freeBytes) {
  freeBytes_ = freeBytes;
  lowStorage_ = freeBytes < kOtaHeadroomBytes;

  if (!enabled) {
    enabled_ = false;
    stopServer();
    if (state_ != NetState::Disabled) {
      WiFi.disconnect();
      WiFi.mode(WIFI_OFF);
    }
    state_ = NetState::Disabled;
    error_ = OtaError::None;
    progress_ = 0;
    ip_[0] = ip_[1] = ip_[2] = ip_[3] = 0;
    rssi_ = 0;
    Serial.println("[net] Wi-Fi & OTA disabled");
    return;
  }

  enabled_ = true;
  if (!configured_) {
    state_ = NetState::Disabled;
    return;
  }
  if (lowStorage_) {
    state_ = NetState::Offline;
    nextAttempt_ = millis() + kRetryIntervalMs;
    Serial.println("[net] OTA reserve low; enable deferred");
    return;
  }

  error_ = OtaError::None;
  startConnection(millis());
}

void Net::registerCallbacks() {
  if (callbacksRegistered_) {
    return;
  }
  callbacksRegistered_ = true;

  ArduinoOTA.onStart([this]() {
    state_ = NetState::OtaActive;
    error_ = OtaError::None;
    progress_ = 0;
    lowStorage_ = false;
    if (start_) {
      start_(context_);
    }
  });

  ArduinoOTA.onProgress([this](unsigned int written, unsigned int total) {
    progress_ = total == 0
                    ? 0
                    : static_cast<uint8_t>(
                          (static_cast<uint64_t>(written) * 100u) / total);
    if (progressCallback_) {
      progressCallback_(context_, progress_);
    }
  });

  ArduinoOTA.onEnd([this]() {
    progress_ = 100;
    if (end_) {
      end_(context_);
    }
  });

  ArduinoOTA.onError([this](ota_error_t error) {
    const OtaError mapped = mapOtaError(error);
    if (error_ == OtaError::None) {
      error_ = mapped;
    }
    state_ = NetState::Connected;
    progress_ = 0;
    if (errorCallback_) {
      errorCallback_(context_, mapped);
    }
  });
}

void Net::startConnection(uint32_t nowMs) {
  if (!enabled_ || !configured_) {
    state_ = NetState::Disabled;
    return;
  }
  if (freeBytes_ < kOtaHeadroomBytes) {
    state_ = NetState::Offline;
    lowStorage_ = true;
    nextAttempt_ = nowMs + kRetryIntervalMs;
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(config::kOtaHostname);
  applyStaticIp();

  Serial.printf("[net] connecting to \"%s\"\n", config::kWifiSsid);
  const int result =
      WiFi.beginNoBlock(config::kWifiSsid, config::kWifiPassword);
  (void)result;

  state_ = NetState::Connecting;
  connectStarted_ = nowMs;
  nextAttempt_ = 0;
  error_ = OtaError::None;
  progress_ = 0;
  lowStorage_ = false;
}

void Net::startServer() {
  if (server_) {
    return;
  }

  registerCallbacks();
  ArduinoOTA.setHostname(config::kOtaHostname);
  ArduinoOTA.setPort(config::kOtaPort);
  if (config::kOtaPassword && config::kOtaPassword[0] != '\0') {
    ArduinoOTA.setPassword(config::kOtaPassword);
  } else {
    Serial.println("[ota] warning: PAINT_OTA_PASSWORD is empty");
  }
  ArduinoOTA.setRebootOnSuccess(true);
  ArduinoOTA.begin();
  server_ = true;

  Serial.printf("[ota] listening on %s.local:%u\n", config::kOtaHostname,
                static_cast<unsigned>(config::kOtaPort));
}

void Net::stopServer() {
  if (!server_) {
    return;
  }
  ArduinoOTA.end();
  server_ = false;
  Serial.println("[ota] server stopped");
}

void Net::handleDisconnect(uint32_t nowMs) {
  stopServer();
  state_ = NetState::Offline;
  nextAttempt_ = nowMs + kRetryIntervalMs;
  ip_[0] = ip_[1] = ip_[2] = ip_[3] = 0;
  rssi_ = 0;
  progress_ = 0;
  Serial.printf("[net] disconnected; retry in %lu s\n",
                static_cast<unsigned long>(kRetryIntervalMs / 1000));
}

void Net::updateDetails(uint32_t nowMs) {
  if (static_cast<int32_t>(nowMs - nextDetails_) < 0) {
    return;
  }
  nextDetails_ = nowMs + kDetailsIntervalMs;

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const IPAddress ip = WiFi.localIP();
  for (int i = 0; i < 4; ++i) {
    ip_[i] = ip[i];
  }
  rssi_ = WiFi.RSSI();
}

void Net::loop(uint32_t nowMs, size_t freeBytes) {
  freeBytes_ = freeBytes;
  lowStorage_ = freeBytes_ < kOtaHeadroomBytes;

  if (!enabled_ || !configured_) {
    state_ = NetState::Disabled;
    return;
  }

  switch (state_) {
    case NetState::Disabled:
      if (freeBytes_ >= kOtaHeadroomBytes) {
        startConnection(nowMs);
      } else {
        state_ = NetState::Offline;
        nextAttempt_ = nowMs + kRetryIntervalMs;
      }
      break;

    case NetState::Offline:
      if (!lowStorage_ &&
          static_cast<int32_t>(nowMs - nextAttempt_) >= 0) {
        startConnection(nowMs);
      }
      break;

    case NetState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        state_ = NetState::Connected;
        error_ = OtaError::None;
        nextDetails_ = 0;
        updateDetails(nowMs);
        Serial.printf("[net] connected to \"%s\"\n", config::kWifiSsid);
      } else if (nowMs - connectStarted_ >= kConnectTimeoutMs) {
        WiFi.disconnect();
        state_ = NetState::Offline;
        nextAttempt_ = nowMs + kRetryIntervalMs;
        Serial.printf("[net] connect timeout; retry in %lu s\n",
                      static_cast<unsigned long>(kRetryIntervalMs / 1000));
      }
      break;

    case NetState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        handleDisconnect(nowMs);
        break;
      }
      updateDetails(nowMs);
      if (lowStorage_) {
        stopServer();
        break;
      }
      if (!server_) {
        startServer();
      }
      // ArduinoOTA handles MDNS and, when a transfer has been negotiated,
      // runs the blocking receive loop. Its callbacks paint progress directly.
      if (server_) {
        ArduinoOTA.handle();
      }
      break;

    case NetState::OtaActive:
      // Normally the transfer runs inside ArduinoOTA.handle() and the state is
      // back to Connected before control returns. Keep servicing the server if
      // a callback left us here.
      if (server_) {
        ArduinoOTA.handle();
      }
      break;
  }

  if (state_ == NetState::Connected) {
    updateDetails(nowMs);
  }
}

NetInfo Net::info() const {
  NetInfo result;
  result.state = state_;
  result.enabled = enabled_;
  result.configured = configured_;
  result.server = server_;
  result.lowStorage = lowStorage_ || freeBytes_ < kOtaHeadroomBytes;
  std::memcpy(result.ip, ip_, sizeof(result.ip));
  result.rssi = rssi_;
  result.progress = progress_;
  result.error = error_;
  return result;
}

}  // namespace paint