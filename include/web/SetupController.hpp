#pragma once

#include <ESPAsyncWebServer.h>

class SetupController {
public:
  SetupController();
  void setup();
  void start();
  void stop();

private:
  bool running{false};
  AsyncWebServer server{80};
  AsyncEventSource events{"/events"};

  void handleFile(AsyncWebServerRequest *request);
  String getContentType(String filename);
};
