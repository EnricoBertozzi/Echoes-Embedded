#pragma once

#include "application/hardware/dto/WifiConnectDto.hpp"
#include "application/hardware/dto/WifiScanDto.hpp"
#include <vector>

class WifiSTAObserver {
public:
  ~WifiSTAObserver() = default;

  virtual void onConnect(const WifiConnectDto &connectDto) {}
  virtual void onDisconnect(const WifiConnectDto &connectDto) {}
  virtual void onScan(const std::vector<WifiScanDto> &scanList) {}
};
