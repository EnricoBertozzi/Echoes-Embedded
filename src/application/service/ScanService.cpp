#include "application/service/ScanService.hpp"

void ScanService::onConnect() {
  count++;
  if (!wifiSTA.isScanning()) {
    wifiSTA.startScan();
  }
}

void ScanService::onDisconnect() {
  count--;
  if (count < 1 && wifiSTA.isScanning()) {
    wifiSTA.stopScan();
  }
}
