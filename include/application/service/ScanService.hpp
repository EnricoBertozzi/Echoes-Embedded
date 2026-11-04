#pragma once

#include "application/hardware/WifiAPObserver.hpp"
#include "application/hardware/WifiSTA.hpp"
#include <cstdint>

class ScanService : public WifiAPObserver {
public:
  ScanService(WifiSTA &wifiSTA) : wifiSTA(wifiSTA) {}
  void onConnect() override;
  void onDisconnect() override;

private:
  uint8_t count;
  WifiSTA &wifiSTA;
};
