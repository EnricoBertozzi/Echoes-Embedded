#pragma once

#include "application/hardware/WifiAP.hpp"
#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiStorage.hpp"

class WifiService {
public:
  WifiService(WifiAP &ap, WifiSTA &sta, WifiStorage &storage)
      : ap(ap), sta(sta), storage(storage) {}
  void start();
  void stop();

private:
  WifiAP &ap;
  WifiSTA &sta;
  WifiStorage &storage;
};
