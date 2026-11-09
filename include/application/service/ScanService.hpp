#pragma once

#include "application/hardware/WifiAPObserver.hpp"
#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/service/DeviceRegisterObserver.hpp"
#include <cstdint>
#include <vector>

// Scan policy:
// - scans from boot until the device connects to a Wi-Fi network (STA);
// - keeps scanning while a client is on the setup AP, even with the STA
//   connected, because a client on the portal probably wants to change
//   networks; stops again once the client leaves;
// - resumes whenever the STA disconnects or fails to connect;
// - pauses while the device registration talks to the broker: the TLS
//   handshake needs the radio, and a concurrent scan stretches it past
//   the connect timeout.
//
// The ESP32 has a single radio, so the scan task paces its cycles (short
// per-channel dwell + pause between cycles) to leave the SoftAP enough
// airtime for beacons and data while someone is browsing the portal.
class ScanService : public WifiAPObserver,
                    public WifiSTAObserver,
                    public DeviceRegisterObserver {
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

  // DeviceRegisterObserver
  void onRegistering() override;
  void onRegistered(const std::string &status,
                    const std::string &deviceId) override;
  void onRejected(const std::string &message, bool preProvisioned) override;
  void onRetryLater(const std::string &reason, bool keepingKey) override;
  void onKeyGenerationFailed() override;

private:
  void updateScan();

  uint8_t apClients{0};
  bool staConnected{false};
  WifiSTA &wifiSTA;
};

