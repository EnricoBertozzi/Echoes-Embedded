#pragma once

#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/hardware/dto/WifiScanDto.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/tasks.h>

class EspWifiSTA : public WifiSTA {
public:
  EspWifiSTA();
  ~EspWifiSTA() override;
  void setup();
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
  bool scanning;
  TaskHandle_t scanTask;
  WifiScanDto statusNetwork;

  void processScan();
  WifiScanDto processScanItem(int i);
};
