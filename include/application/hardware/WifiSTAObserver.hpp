#pragma once

#include "application/hardware/dto/WifiScanDto.hpp"
#include <vector>

class WifiSTAObserver {
public:
  ~WifiSTAObserver() = default;

  virtual void onConnect(const WifiScanDto &connectDto) {}
  virtual void onError(const WifiScanDto &connectDto) {}
  virtual void onDisconnect(const WifiScanDto &connectDto) {}
  virtual void onScan(const std::vector<WifiScanDto> &scanList) {}
};
