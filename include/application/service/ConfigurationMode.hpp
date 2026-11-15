#pragma once

#include "application/hardware/WifiAP.hpp"
#include "application/service/ConfigurationModeObserver.hpp"
#include "application/service/PortalActivity.hpp"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <vector>

// A client on the setup AP means configuration mode, but only while the
// portal is actually being used: phones stay associated to the "no
// internet" network long after the user stopped configuring, and the Wi-Fi
// disconnect event is not always delivered. Association plus recent portal
// activity is the signal both the scan policy and the platform round need.
class ConfigurationMode {
public:
  ConfigurationMode(WifiAP &wifiAP, PortalActivity &activity)
      : wifiAP(wifiAP), activity(activity) {}
  ~ConfigurationMode();

  void begin();
  bool isActive() const { return active.load(); }
  void registerObserver(ConfigurationModeObserver *observer);
  void unregisterObserver(ConfigurationModeObserver *observer);

private:
  WifiAP &wifiAP;
  PortalActivity &activity;

  std::atomic<bool> active{false};
  std::vector<ConfigurationModeObserver *> observers;
  std::mutex observersMutex;
  TaskHandle_t task{nullptr};

  static void taskEntry(void *param);
  void run();
};
