#pragma once

#include "application/hardware/dto/WifiConnectDto.hpp"
class WifiSTA {
public:
  ~WifiSTA() = default;

  virtual void connect(const WifiConnectDto &connectDto) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() = 0;
  virtual bool isScanning() = 0;
  virtual void startScan() = 0;
  virtual void stopScan() = 0;
};
