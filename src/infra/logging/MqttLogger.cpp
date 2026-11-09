#include "infra/logging/MqttLogger.hpp"
#include <Arduino.h>

void MqttLogger::onConnected() {
  Serial.println("[MQTT] connected");
}

void MqttLogger::onError(const std::string &reason) {
  Serial.printf("[MQTT] error: %s\n", reason.c_str());
}
