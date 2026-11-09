#pragma once

#include "application/service/DeviceRegisterObserver.hpp"

class DeviceRegisterLogger : public DeviceRegisterObserver {
public:
  void onGeneratingKey(const std::string &mac) override;
  void onKeyGenerationFailed() override;
  void onKeyLoaded() override;
  void onRegistered(const std::string &status,
                    const std::string &deviceId) override;
  void onRejected(const std::string &message, bool preProvisioned) override;
  void onRetryLater(const std::string &reason, bool keepingKey) override;
  void onKeySaved() override;
  void onKeySaveFailed() override;
  void onTokenIssued(const std::string &token) override;
  void onClockNotSynced() override;
};
