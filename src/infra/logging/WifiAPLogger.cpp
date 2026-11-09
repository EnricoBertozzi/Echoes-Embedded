#include "infra/logging/WifiAPLogger.hpp"
#include <Arduino.h>

void WifiAPLogger::onConnect() {
  clients++;
  Serial.printf("[AP] client joined (%u using the portal)\n", clients);
}

void WifiAPLogger::onDisconnect() {
  if (clients > 0) {
    clients--;
  }
  Serial.printf("[AP] client left (%u using the portal)\n", clients);
}
