#include "application/service/WifiService.hpp"

void WifiService::start() {
  ap.start();

  WifiConnectDto dto;
  auto loaded = storage.load(dto);
  if (loaded) {
    sta.connect(dto);
  }
}

void WifiService::stop() {
  ap.stop();
  if (sta.isConnected()) {
    sta.disconnect();
  }
}
