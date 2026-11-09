#pragma once

#include "application/hardware/WifiAPObserver.hpp"
#include <cstdint>

class WifiAPLogger : public WifiAPObserver {
public:
  void onConnect() override;
  void onDisconnect() override;

private:
  uint8_t clients{0};
};
