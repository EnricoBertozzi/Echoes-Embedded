#include "web/SetupController.hpp"
#include "application/hardware/dto/WifiConnectDto.hpp"
#include <ArduinoJson.h>
#include <LittleFS.h>

SetupController::~SetupController() { delete setupHandler; }

void SetupController::setup() {
  setupHandler = new AsyncCallbackJsonWebHandler(
      "/wifi-setup", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        this->handleSetup(request, json);
      });
  setupHandler->setMethod(HTTP_POST);
  setupHandler->setMaxContentLength(2048);
  server.addHandler(setupHandler);
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

void SetupController::handleSetup(AsyncWebServerRequest *request,
                                  JsonVariant &json) {
  if (request->method() != HTTP_POST) {
    request->send(405, "text/plain", "Method not allowed");
    return;
  }
  JsonObject obj = json.as<JsonObject>();
  String ssid = obj["ssid"];
  String password = obj["password"];
  String ip = obj["ip"];
  String gateway = obj["gateway"];
  String subnet = obj["subnet"];
  WifiConnectDto dto{ssid.c_str(), password.c_str(), ip.c_str(),
                     gateway.c_str(), subnet.c_str()};

  wifiSTA.connect(dto);

  auto *response = request->beginResponse(200);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Connection", "close");
  request->send(response);
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

  if (!response) {
    request->send(404, "text/plain", "Not Found");
    return;
  }

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

void SetupController::onConnect(const WifiScanDto &connectDto) {
  updateStatus(connectDto, WifiStatus::WIFI_CONNECTED);
}

void SetupController::onConnecting(const WifiScanDto &connectDto) {
  updateStatus(connectDto, WifiStatus::WIFI_CONNECTING);
}

void SetupController::onError(const WifiScanDto &connectDto) {
  updateStatus(connectDto, WifiStatus::WIFI_ERROR);
}

void SetupController::onDisconnect(const WifiScanDto &connectDto) {
  updateStatus(connectDto, WifiStatus::WIFI_NONE);
}

void SetupController::updateStatus(const WifiScanDto &connectDto,
                                   WifiStatus status) {
  JsonDocument doc;
  doc["ssid"] = connectDto.ssid;
  doc["status"] = static_cast<int>(connectDto.status);
  doc["rssi"] = connectDto.rssi;
  doc["secure"] = connectDto.secure;
  String json;
  serializeJson(doc, json);
  events.send(json, "status");
}

void SetupController::onScan(const std::vector<WifiScanDto> &scanList) {
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (auto &network : scanList) {
    JsonObject o = array.add<JsonObject>();
    o["ssid"] = network.ssid;
    o["secure"] = network.secure;
    o["rssi"] = network.rssi;
    o["status"] = static_cast<int>(network.status);
  }
  String json;
  serializeJson(doc, json);
  events.send(json, "list");
}
