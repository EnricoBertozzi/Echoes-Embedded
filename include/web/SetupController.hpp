#pragma once

#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/hardware/dto/WifiScanDto.hpp"
#include <ESPAsyncWebServer.h>
#include <mutex>
#include <vector>

class SetupController : public WifiSTAObserver {
public:
  SetupController(WifiSTA &wifiSTA) : wifiSTA(wifiSTA) {}
  ~SetupController();
  void setup();
  void start();
  void stop();

  // WifiSTAObserver
  void onConnect(const WifiScanDto &connectDto) override;
  void onConnecting(const WifiScanDto &connectDto) override;
  void onError(const WifiScanDto &connectDto) override;
  void onDisconnect(const WifiScanDto &connectDto) override;
  void onScan(const std::vector<WifiScanDto> &scanList) override;

private:
  bool running{false};
  AsyncWebServer server{80};
  AsyncEventSource events{"/events"};
  AsyncCallbackJsonWebHandler *setupHandler{nullptr};
  WifiSTA &wifiSTA;

  // Last scan list, served on GET /wifi-list so the page can render
  // something before the first SSE event arrives.
  std::mutex scanMutex;
  std::vector<WifiScanDto> lastScan;

  void handleSetup(AsyncWebServerRequest *request, JsonVariant &json);
  void handleScanList(AsyncWebServerRequest *request);
  void handleFile(AsyncWebServerRequest *request);
  String getContentType(String filename);
  String serializeScanList(const std::vector<WifiScanDto> &scanList) const;

  // WifiSTAObserver
  void updateStatus(const WifiScanDto &connectDto, WifiStatus status);
};
