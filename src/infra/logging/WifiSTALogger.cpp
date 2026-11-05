#include "infra/logging/WifiSTALogger.hpp"
#include <Arduino.h>

void WifiSTALogger::onConnect(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] Connected at %s\n", connectDto.ssid.c_str());
}

void WifiSTALogger::onConnecting(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] connecting at %s\n", connectDto.ssid.c_str());
}

void WifiSTALogger::onError(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] error while connecting at %s\n",
                connectDto.ssid.c_str());
}

void WifiSTALogger::onDisconnect(const WifiScanDto &connectDto) {
  Serial.printf("[WifiSTA] Disconnected from %s\n", connectDto.ssid.c_str());
}

void WifiSTALogger::onScan(const std::vector<WifiScanDto> &scanList) {
  Serial.printf("[WifiSTA] scan: %d networks\n", (int)scanList.size());
}
