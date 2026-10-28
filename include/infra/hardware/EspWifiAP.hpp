#pragma once

#include <DNSServer.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <vector>

#include "application/hardware/WifiAP.hpp"
#include "application/hardware/WifiAPObserver.hpp"

class EspWifiAP : public WifiAP {
public:
  EspWifiAP();
  void start() override;
  void stop() override;
  void registerObserver(WifiAPObserver *observer) override;
  void unregisterObserver(WifiAPObserver *observer) override;

private:
  bool running{false};
  std::vector<WifiAPObserver *> observers;
  DNSServer dns;
  const byte DNS_PORT{53};
  uint16_t countConnect;

  IPAddress apIP{192, 168, 4, 1};
  IPAddress gateway{192, 168, 4, 1};
  IPAddress subnet{255, 255, 255, 0};

  void onWifiEvent(WiFiEvent_t event);
};
