#include "infra/hardware/EspWifiAP.hpp"
#include <algorithm>

EspWifiAP::EspWifiAP() {
  WiFi.onEvent([this](WiFiEvent_t event) {});
}

void EspWifiAP::start() {}

void EspWifiAP::stop() {}

void EspWifiAP::registerObserver(WifiAPObserver *observer) {
  observers.push_back(observer);
}

void EspWifiAP::unregisterObserver(WifiAPObserver *observer) {
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

void EspWifiAP::onWifiEvent(WiFiEvent_t event) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    countConnect++;
    for (auto &obs : observers)
      obs->onConnect();
    break;

  case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
    countConnect--;
    for (auto &obs : observers)
      obs->onDisconnect();
    break;
  default:
    break;
  }
}
