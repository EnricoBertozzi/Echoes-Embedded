#pragma once

#include <string>

enum class WifiStatus {
  WIFI_NONE = 0,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_ERROR
};

struct WifiScanDto {
  std::string ssid;
  bool secure;
  WifiStatus status;
  int32_t rssi;
};
