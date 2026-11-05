#pragma once

#include "application/hardware/WifiSTAObserver.hpp"

class WifiSTALogger : public WifiSTAObserver {
public:
  void onConnect(const WifiScanDto &connectDto) override;
  void onConnecting(const WifiScanDto &connectDto) override;
  void onError(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
  void onScan(const std::vector<WifiScanDto> &scanList) override;
};
