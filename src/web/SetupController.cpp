#include "web/SetupController.hpp"
#include <LittleFS.h>

SetupController::SetupController() {}

void SetupController::setup() {
  server.addHandler(&events);
  server.onNotFound(
      [this](AsyncWebServerRequest *request) { this->handleFile(request); });
}

void SetupController::start() {
  if (running) {
    return;
  }
  server.begin();
}

void SetupController::stop() {
  if (!running) {
    return;
  }
  server.end();
}

void SetupController::handleFile(AsyncWebServerRequest *request) {
  String path = request->url();

  if (path == "/")
    path = "/index.html";

  if (!LittleFS.exists(path))
    path = "/index.html";

  String contentType = getContentType(path);

  AsyncWebServerResponse *response =
      request->beginResponse(LittleFS, path, contentType);

  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Connection", "close");

  request->send(response);
}

String SetupController::getContentType(String filename) {
  if (filename.endsWith(".html"))
    return "text/html";
  if (filename.endsWith(".css"))
    return "text/css";
  if (filename.endsWith(".js"))
    return "application/javascript";
  if (filename.endsWith(".png"))
    return "image/png";
  if (filename.endsWith(".jpg"))
    return "image/jpeg";
  if (filename.endsWith(".svg"))
    return "image/svg+xml";
  if (filename.endsWith(".ico"))
    return "image/x-icon";
  return "text/plain";
}
