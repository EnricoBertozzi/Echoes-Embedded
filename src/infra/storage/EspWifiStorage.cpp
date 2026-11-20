#include "infra/storage/EspWifiStorage.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace {
// Unique namespace/key so the app does not collide with the Wi-Fi driver's
// own NVS entries.
constexpr const char *NVS_NAMESPACE = "echoes_wifi";
constexpr const char *NVS_KEY = "cfg";
} // namespace

bool EspWifiStorage::save(const WifiConnectDto &dto) {
  JsonDocument doc;
  doc["ssid"] = dto.ssid;
  doc["password"] = dto.password;
  doc["ip"] = dto.ip;
  doc["gateway"] = dto.gateway;
  doc["subnet"] = dto.subnet;

  String payload;
  serializeJson(doc, payload);

  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("[WifiStorage] failed to open NVS for writing");
    return false;
  }

  const bool saved =
      preferences.putString(NVS_KEY, payload.c_str()) == payload.length();
  preferences.end();

  if (!saved) {
    Serial.println("[WifiStorage] failed to save Wi-Fi credentials");
  }
  return saved;
}

bool EspWifiStorage::load(WifiConnectDto &dto) {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    return false;
  }

  const String payload = preferences.getString(NVS_KEY, "");
  preferences.end();

  if (payload.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    Serial.println("[WifiStorage] invalid Wi-Fi credentials in NVS");
    return false;
  }

  const char *ssid = doc["ssid"] | "";
  if (ssid == nullptr || *ssid == '\0') {
    return false;
  }

  dto.ssid = ssid;
  dto.password = doc["password"] | "";
  dto.ip = doc["ip"] | "";
  dto.gateway = doc["gateway"] | "";
  dto.subnet = doc["subnet"] | "";
  return true;
}

bool EspWifiStorage::clear() {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    return true;
  }
  const bool cleared = preferences.clear();
  preferences.end();
  return cleared;
}
