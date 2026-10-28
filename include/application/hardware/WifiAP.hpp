#pragma once

#include "application/hardware/WifiAPObserver.hpp"

class WifiAP {
public:
  virtual ~WifiAP() = default;

  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void registerObserver(WifiAPObserver *observer);
  virtual void unregisterObserver(WifiAPObserver *observer);
};
