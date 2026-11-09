#pragma once

#include <string>

class DeviceRegisterObserver {
public:
  virtual ~DeviceRegisterObserver() = default;

  // The MQTT round is starting: the radio is needed for TLS.
  virtual void onRegistering() {}
  virtual void onGeneratingKey(const std::string &mac) {}
  virtual void onKeyGenerationFailed() {}
  virtual void onKeyLoaded() {}
  virtual void onRegistered(const std::string &status,
                            const std::string &deviceId) {}
  virtual void onRejected(const std::string &message, bool preProvisioned) {}
  virtual void onRetryLater(const std::string &reason, bool keepingKey) {}
  virtual void onKeySaved() {}
  virtual void onKeySaveFailed() {}
  virtual void onTokenIssued(const std::string &token) {}
  virtual void onClockNotSynced() {}
};
