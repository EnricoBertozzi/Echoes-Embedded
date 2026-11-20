#pragma once

#include "application/hardware/WifiAP.hpp"
#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/hardware/WifiStorage.hpp"
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <string>
#include <vector>

// Owns the STA connection lifecycle:
// - loads the last successfully used credentials from storage at boot;
// - saves new credentials only after the STA actually gets an IP;
// - periodically tries to reconnect to the saved network when it is visible
//   in a scan and the STA is disconnected.
class WifiService : public WifiSTAObserver {
public:
  WifiService(WifiAP &ap, WifiSTA &sta, WifiStorage &storage)
      : ap(ap), sta(sta), storage(storage) {}
  ~WifiService() override;

  void start();
  void stop();
  void connect(const WifiConnectDto &dto);

  // WifiSTAObserver
  void onConnect(const WifiScanDto &connectDto) override;
  void onError(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
  void onScan(const std::vector<WifiScanDto> &scanList) override;

private:
  WifiAP &ap;
  WifiSTA &sta;
  WifiStorage &storage;

  TaskHandle_t task{nullptr};
  std::mutex mutex;

  WifiConnectDto saved;
  WifiConnectDto pending;
  bool hasSaved{false};
  bool hasPending{false};
  uint32_t pendingGeneration{0};
  bool attemptPending{false};
  TickType_t attemptStartedTick{0};
  bool started{false};
  std::vector<std::string> visibleNetworks;

  static void taskEntry(void *param);
  void run();
  void tryReconnect();
  bool networkVisibleLocked(const std::string &ssid) const;
};
