#pragma once

#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/hardware/dto/WifiScanDto.hpp"
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <vector>

class EspWifiSTA : public WifiSTA {
public:
  EspWifiSTA();
  ~EspWifiSTA() override;
  void setup() override;
  void connect(const WifiConnectDto &connectDto) override;
  void disconnect() override;
  bool isConnected() override;
  bool isScanning() override;
  void startScan() override;
  void stopScan() override;

  void registerObserver(WifiSTAObserver *observer) override;
  void unregisterObserver(WifiSTAObserver *observer) override;

private:
  // Shared between the WiFi event task, the scan task and the proxy task:
  // every access goes through the corresponding mutex.
  std::vector<WifiSTAObserver *> observers;
  std::mutex observersMutex;
  std::atomic<bool> scanning{false};
  std::mutex scanMutex;
  TaskHandle_t scanTask{nullptr};
  WifiScanDto statusNetwork;
  std::mutex statusMutex;

  void connectDHCP(const WifiConnectDto &connectCmd);
  void connectStatic(const WifiConnectDto &connectCmd);
  void connect(const std::string &ssid, const std::string &pass);
  void processScan();
  WifiScanDto processScanItem(int i);
  void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

  WifiScanDto snapshotStatus();
  WifiScanDto setErrorStatus();
  std::vector<WifiSTAObserver *> lockedObservers();
};
