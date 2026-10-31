#pragma once

#include "application/hardware/WifiSTA.hpp"
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/hardware/dto/WifiScanDto.hpp"
#include <ESPAsyncWebServer.h>

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
  AsyncCallbackJsonWebHandler *setupHandler;
  WifiSTA &wifiSTA;

  void handleSetup(AsyncWebServerRequest *request, JsonVariant &json);
  void handleFile(AsyncWebServerRequest *request);
  String getContentType(String filename);

  // WifiSTAObserver
  void updateStatus(const WifiScanDto &connectDto, WifiStatus status);
};
