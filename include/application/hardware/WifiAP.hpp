#pragma once

#include "application/hardware/WifiAPObserver.hpp"
#include <cstddef>

class WifiAP {
public:
  virtual ~WifiAP() = default;

  virtual void start() = 0;
  virtual void stop() = 0;
  // Clients currently associated with the setup AP.
  virtual std::size_t countConnect() = 0;
  virtual void registerObserver(WifiAPObserver *observer) = 0;
  virtual void unregisterObserver(WifiAPObserver *observer) = 0;
};
