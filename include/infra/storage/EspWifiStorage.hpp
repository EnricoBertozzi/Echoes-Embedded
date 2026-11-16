#pragma once

#include "application/hardware/WifiStorage.hpp"

// Persists the last successfully used Wi-Fi credentials in NVS. The setup
// AP credentials are fixed, so only the STA connection data is stored here.
class EspWifiStorage : public WifiStorage {
public:
  bool save(const WifiConnectDto &dto) override;
  bool load(WifiConnectDto &dto) override;
  bool clear() override;
};
