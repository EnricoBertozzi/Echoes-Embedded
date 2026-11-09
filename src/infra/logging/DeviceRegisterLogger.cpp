#include "infra/logging/DeviceRegisterLogger.hpp"
#include <Arduino.h>

void DeviceRegisterLogger::onGeneratingKey(const std::string &mac) {
  Serial.printf("[Register] no private.pem: generating an ES256 key pair "
                "for %s\n",
                mac.c_str());
}

void DeviceRegisterLogger::onKeyGenerationFailed() {
  Serial.println("[Register] key pair generation failed");
}

void DeviceRegisterLogger::onKeyLoaded() {
  Serial.println("[Register] private.pem loaded; (re)registering with the "
                 "platform");
}

void DeviceRegisterLogger::onRegistered(const std::string &status,
                                        const std::string &deviceId) {
  Serial.printf("[Register] registered (%s) deviceId=%s\n", status.c_str(),
                deviceId.c_str());
}

void DeviceRegisterLogger::onRejected(const std::string &message,
                                      bool preProvisioned) {
  Serial.printf("[Register] registration REJECTED: %s\n", message.c_str());
  if (preProvisioned) {
    Serial.println("[Register] CRITICAL: the pre-provisioned key conflicts "
                   "with the platform registration for this MAC; fix it on "
                   "the server side (device-api.md section 1)");
  }
}

void DeviceRegisterLogger::onRetryLater(const std::string &reason,
                                        bool keepingKey) {
  Serial.printf("[Register] %s\n", reason.c_str());
  if (keepingKey) {
    Serial.println("[Register] keeping the pre-provisioned key; the "
                   "platform will confirm on a later connection");
  }
}

void DeviceRegisterLogger::onKeySaved() {
  Serial.println("[Register] private.pem saved");
}

void DeviceRegisterLogger::onKeySaveFailed() {
  Serial.println("[Register] CRITICAL: registered but private.pem could "
                 "not be saved");
}

void DeviceRegisterLogger::onTokenIssued(const std::string &token) {
  Serial.printf("[Register] platform access token: %s\n", token.c_str());
}

void DeviceRegisterLogger::onClockNotSynced() {
  Serial.println("[Register] clock not synced yet; the token will be "
                 "issued on demand");
}
