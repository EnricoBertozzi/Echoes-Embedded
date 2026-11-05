#pragma once

#include <cstdint>
#include <string>

enum class WifiStatus {
  WIFI_NONE = 0,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_ERROR
};

struct WifiScanDto {
  std::string ssid;
  bool secure{false};
  WifiStatus status{WifiStatus::WIFI_NONE};
  int32_t rssi{0};
};
