#pragma once

#include <string>

struct WifiConnectDto {
  std::string ssid;
  std::string password;
  std::string ip;
  std::string gateway;
  std::string subnet;

  bool isDHCP() const { return ip.empty(); }
};
