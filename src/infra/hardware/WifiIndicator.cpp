#include "infra/hardware/WifiIndicator.hpp"
#include <Arduino.h>

WifiIndicator::WifiIndicator() {
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
}

void WifiIndicator::onConnect(const WifiScanDto &connectDto) {
  digitalWrite(2, HIGH);
}

void WifiIndicator::onDisconnect(const WifiScanDto &connectDto) {
  digitalWrite(2, LOW);
}
