#include "infra/logging/MqttLogger.hpp"
#include <Arduino.h>
#include "application/service/RemoteLog.hpp"

void MqttLogger::onConnected() {
  Serial.println("[MQTT] connected");
  REMOTE_LOG(Info, Mqtt, "MQTT_CONNECTED", "Conectado ao broker MQTT.");
}

void MqttLogger::onError(const std::string &reason) {
  Serial.printf("[MQTT] error: %s\n", reason.c_str());
  REMOTE_LOG(Error, Mqtt, "MQTT_ERROR",
             "Erro no MQTT: %s. Cheque internet, MQTT_BROKER_URL, certificado TLS e relógio (SNTP).",
             reason.c_str());
}
