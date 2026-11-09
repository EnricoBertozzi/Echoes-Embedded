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
  // EspWifiSTA::connect() already stops the scan itself.
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
  // Consumed by the web controller.
}

void ScanService::onRegistering() {
  // The TLS handshake needs the radio quiet; updateScan() re-applies the
  // policy when the registration round ends.
  wifiSTA.stopScan();
}

void ScanService::onRegistered(const std::string & /*status*/,
                               const std::string & /*deviceId*/) {
  updateScan();
}

void ScanService::onRejected(const std::string & /*message*/,
                             bool /*preProvisioned*/) {
  updateScan();
}

void ScanService::onRetryLater(const std::string & /*reason*/,
                               bool /*keepingKey*/) {
  updateScan();
}

void ScanService::onKeyGenerationFailed() { updateScan(); }

void ScanService::updateScan() {
  // A client on the setup AP probably wants to change networks, so keep
  // the list fresh even with the STA connected.
  const bool shouldScan = !staConnected || apClients > 0;
  if (shouldScan) {
    wifiSTA.startScan();
  } else {
    wifiSTA.stopScan();
  }
}
