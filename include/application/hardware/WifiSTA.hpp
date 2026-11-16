#pragma once

#include "application/hardware/WifiSTAObserver.hpp"
#include "application/hardware/dto/WifiConnectDto.hpp"
class WifiSTA {
public:
  virtual ~WifiSTA() = default;

  virtual void setup() {}
  virtual void connect(const WifiConnectDto &connectDto) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() = 0;
  virtual bool isScanning() = 0;
  virtual void startScan() = 0;
  virtual void stopScan() = 0;
  // Stops scanning from the calling task. Implementations that serialize
  // Wi-Fi commands must bypass their queue: callers use this when the radio
  // has to be quiet before a blocking TLS handshake.
  virtual void stopScanNow() { stopScan(); }
  virtual void registerObserver(WifiSTAObserver *observer) = 0;
  virtual void unregisterObserver(WifiSTAObserver *observer) = 0;
};
