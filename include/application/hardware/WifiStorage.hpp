#pragma once

#include "application/hardware/dto/WifiConnectDto.hpp"
class WifiStorage {
public:
  ~WifiStorage() = default;

  virtual bool save(const WifiConnectDto &dto) = 0;
  virtual bool load(WifiConnectDto &dto) = 0;
  virtual bool clear() = 0;
};
