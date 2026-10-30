#include "infra/hardware/EspWifiSTA.hpp"

void EspWifiSTA::registerObserver(WifiSTAObserver *observer) {
  observers.push_back(observer);
}

void EspWifiSTA::unregisterObserver(WifiSTAObserver *observer) {
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}
