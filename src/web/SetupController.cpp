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
  server.on("/wifi-list", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleScanList(request);
  });
  server.onNotFound(
      [this](AsyncWebServerRequest *request) { this->handleFile(request); });
}

void SetupController::start() {
  if (running) {
    return;
  }
  server.begin();
  running = true;
}

void SetupController::stop() {
  if (!running) {
    return;
  }
  server.end();
  running = false;
}

uint32_t SetupController::idleMs() const {
  return millis() - lastRequestMs.load();
}

void SetupController::touch() { lastRequestMs.store(millis()); }

void SetupController::handleSetup(AsyncWebServerRequest *request,
                                  JsonVariant &json) {
  touch();
  JsonObject obj = json.as<JsonObject>();
  // Missing keys must become empty strings. A plain `String x = obj["key"]`
  // serializes a null JsonVariant to the literal "null", which would make
  // isDHCP() false and send a DHCP request down the static-IP path.
  String ssid = obj["ssid"] | "";
  String password = obj["password"] | "";
  String ip = obj["ip"] | "";
  String gateway = obj["gateway"] | "";
  String subnet = obj["subnet"] | "";

  if (ssid.isEmpty()) {
    request->send(400, "text/plain", "Missing ssid");
    return;
  }

  WifiConnectDto dto{ssid.c_str(), password.c_str(), ip.c_str(),
                     gateway.c_str(), subnet.c_str()};

  wifiSTA.connect(dto);

  auto *response = request->beginResponse(200);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Connection", "close");
  request->send(response);
}

void SetupController::handleScanList(AsyncWebServerRequest *request) {
  touch();
  String json;
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    json = serializeScanList(lastScan);
  }
  auto *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void SetupController::handleFile(AsyncWebServerRequest *request) {
  touch();
  String path = request->url();

  if (path == "/")
    path = "/index.html";

  // Only web assets are served. Anything else — captive portal probes and
  // device files such as /private.pem — falls back to the app page, never
  // to the filesystem.
  const bool servable = path == "/index.html" || path == "/icon.svg" ||
                        path.startsWith("/styles/") ||
                        path.startsWith("/scripts/");
  if (!servable || !LittleFS.exists(path))
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

String SetupController::serializeScanList(
    const std::vector<WifiScanDto> &scanList) const {
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
  return json;
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
  doc["status"] = static_cast<int>(status);
  doc["rssi"] = connectDto.rssi;
  doc["secure"] = connectDto.secure;
  String json;
  serializeJson(doc, json);
  events.send(json, "status");
}

void SetupController::onScan(const std::vector<WifiScanDto> &scanList) {
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    lastScan = scanList;
  }
  events.send(serializeScanList(scanList), "list");
}
