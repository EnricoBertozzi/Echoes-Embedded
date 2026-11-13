#pragma once

#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/service/ConfigurationModeObserver.hpp"
#include "application/service/DeviceRegisterObserver.hpp"
#include <atomic>
#include <vector>

// Scan policy:
// - scans from boot until the device connects to a Wi-Fi network (STA);
// - keeps scanning while the device is in configuration mode (a client using
//   the setup portal), even with the STA connected, because that client
//   probably wants to change networks; stops when configuration mode ends;
// - resumes whenever the STA disconnects or fails to connect;
// - pauses while the device registration talks to the broker: the TLS
//   handshake needs the radio, and a concurrent scan stretches it past
//   the connect timeout.
//
// The ESP32 has a single radio, so the scan task paces its cycles (short
// per-channel dwell + pause between cycles) to leave the SoftAP enough
// airtime for beacons and data while someone is browsing the portal.
class ScanService : public WifiSTAObserver,
                    public ConfigurationModeObserver,
                    public DeviceRegisterObserver {
public:
  explicit ScanService(WifiSTA &wifiSTA) : wifiSTA(wifiSTA) {}

  // Applies the scan policy for the current state (called once at boot).
  void begin();

  // WifiSTAObserver
  void onConnect(const WifiScanDto &connectDto) override;
  void onConnecting(const WifiScanDto &connectDto) override;
  void onError(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
  void onScan(const std::vector<WifiScanDto> &scanList) override;

  // ConfigurationModeObserver
  void onConfigurationModeChanged(bool active) override;

  // DeviceRegisterObserver
  void onRegistering() override;
  void onRegistered(const std::string &status,
                    const std::string &deviceId) override;
  void onRejected(const std::string &message, bool preProvisioned) override;
  void onRetryLater(const std::string &reason, bool keepingKey) override;
  void onKeyGenerationFailed() override;

private:
  void updateScan();

  WifiSTA &wifiSTA;
  // Written by the configuration-mode task, read by the Wi-Fi event task.
  std::atomic<bool> configurationActive{false};
};
