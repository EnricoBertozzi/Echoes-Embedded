#pragma once

#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include <vector>

class EspWifiSTA : public WifiSTA {
public:
  void connect(const WifiConnectDto &connectDto) override;
  void disconnect() override;
  bool isConnected() override;
  bool isScanning() override;
  void startScan() override;
  void stopScan() override;

  void registerObserver(WifiSTAObserver *observer) override;
  void unregisterObserver(WifiSTAObserver *observer) override;

private:
  std::vector<WifiSTAObserver *> observers;
};
