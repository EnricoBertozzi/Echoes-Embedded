#pragma once

#include "application/hardware/WifiAPObserver.hpp"
#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include <cstdint>
#include <vector>

// Scan policy:
// - scans from boot until the device connects to a Wi-Fi network (STA);
// - keeps scanning while a client is using the setup AP, even if the STA is
//   already connected, so the client can still pick a network;
// - resumes scanning whenever the STA disconnects or fails to connect.
class ScanService : public WifiAPObserver, public WifiSTAObserver {
public:
  explicit ScanService(WifiSTA &wifiSTA) : wifiSTA(wifiSTA) {}

  // Applies the scan policy for the current state (called once at boot).
  void begin();

  // WifiAPObserver
  void onConnect() override;
  void onDisconnect() override;

  // WifiSTAObserver
  void onConnect(const WifiScanDto &connectDto) override;
  void onConnecting(const WifiScanDto &connectDto) override;
  void onError(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
  void onScan(const std::vector<WifiScanDto> &scanList) override;

private:
  void updateScan();

  uint8_t apClients{0};
  bool staConnected{false};
  WifiSTA &wifiSTA;
};
