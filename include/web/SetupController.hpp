#pragma once

#include "application/hardware/WifiSTA.hpp"
#include <ESPAsyncWebServer.h>

class SetupController {
public:
  SetupController(WifiSTA &wifiSTA) : wifiSTA(wifiSTA) {}
  ~SetupController();
  void setup();
  void start();
  void stop();

private:
  bool running{false};
  AsyncWebServer server{80};
  AsyncEventSource events{"/events"};
  AsyncCallbackJsonWebHandler *setupHandler;
  WifiSTA &wifiSTA;

  void handleSetup(AsyncWebServerRequest *request, JsonVariant &json);
  void handleFile(AsyncWebServerRequest *request);
  String getContentType(String filename);
};
