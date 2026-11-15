#include "infra/hardware/EspMacAddress.hpp"
#include <cstdio>
#include <esp_mac.h>

namespace {

std::string format(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return std::string(buf);
}

} // namespace

std::string EspMacAddress::get() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  return format(mac);
}
