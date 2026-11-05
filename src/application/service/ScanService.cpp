#include "application/service/ScanService.hpp"

void ScanService::begin() { updateScan(); }

void ScanService::onConnect() {
  apClients++;
  updateScan();
}

void ScanService::onDisconnect() {
  if (apClients > 0) {
    apClients--;
  }
  updateScan();
}

void ScanService::onConnect(const WifiScanDto & /*connectDto*/) {
  staConnected = true;
  updateScan();
}

void ScanService::onConnecting(const WifiScanDto & /*connectDto*/) {
  // Nothing to do: the connect flow stops the scan by itself.
}

void ScanService::onError(const WifiScanDto & /*connectDto*/) {
  staConnected = false;
  updateScan();
}

void ScanService::onDisconnect(const WifiScanDto & /*connectDto*/) {
  staConnected = false;
  updateScan();
}

void ScanService::onScan(const std::vector<WifiScanDto> & /*scanList*/) {
  // Nothing to do: the scan list is consumed by the web controller.
}

void ScanService::updateScan() {
  const bool shouldScan = !staConnected || apClients > 0;
  if (shouldScan) {
    wifiSTA.startScan();
  } else {
    wifiSTA.stopScan();
  }
}
