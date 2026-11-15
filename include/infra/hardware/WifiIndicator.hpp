#pragma once

#include "application/hardware/WifiSTAObserver.hpp"

class WifiIndicator : public WifiSTAObserver {
public:
  WifiIndicator();
  void onConnect(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
};
