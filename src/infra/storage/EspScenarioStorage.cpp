#include "infra/storage/EspScenarioStorage.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace {
constexpr const char *NVS_NAMESPACE = "echoes_scenarios";
constexpr const char *NVS_KEY = "list";

String bytesToHex(const uint8_t *bytes, size_t len) {
  static const char *hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out += hex[(bytes[i] >> 4) & 0xF];
    out += hex[bytes[i] & 0xF];
  }
  return out;
}

bool hexToBytes(const char *hexStr, uint8_t *bytes, size_t len) {
  size_t hexLen = strlen(hexStr);
  if (hexLen != len * 2)
    return false;
  for (size_t i = 0; i < len; i++) {
    char hi = hexStr[i * 2];
    char lo = hexStr[i * 2 + 1];
    auto fromHex = [](char c) -> int {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      return -1;
    };
    int h = fromHex(hi);
    int l = fromHex(lo);
    if (h < 0 || l < 0)
      return false;
    bytes[i] = static_cast<uint8_t>((h << 4) | l);
  }
  return true;
}
} // namespace

bool EspScenarioStorage::saveAll(const std::vector<Scenario> &list) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &scenario : list) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = bytesToHex(scenario.id, 16);
    obj["pointId"] = bytesToHex(scenario.pointId, 16);
    obj["audio"] = String(scenario.audio);
    obj["validUntil"] = static_cast<int64_t>(scenario.validUntil);
  }

  String payload;
  serializeJson(doc, payload);

  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("[ScenarioStorage] failed to open NVS for writing");
    return false;
  }
  const bool ok =
      preferences.putString(NVS_KEY, payload.c_str()) == payload.length();
  preferences.end();
  if (!ok) {
    Serial.println("[ScenarioStorage] failed to save scenarios");
  }
  return ok;
}

bool EspScenarioStorage::loadAll(std::vector<Scenario> &list) {
  list.clear();
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
    Serial.println("[ScenarioStorage] invalid scenarios JSON");
    return false;
  }
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    Scenario scenario{};
    const char *idHex = obj["id"] | "";
    const char *pointHex = obj["pointId"] | "";
    if (!hexToBytes(idHex, scenario.id, 16) ||
        !hexToBytes(pointHex, scenario.pointId, 16)) {
      continue;
    }
    const char *audio = obj["audio"] | "";
    strncpy(scenario.audio, audio, sizeof(scenario.audio) - 1);
    scenario.audio[sizeof(scenario.audio) - 1] = '\0';
    scenario.validUntil = static_cast<time_t>(obj["validUntil"] | 0LL);
    list.push_back(scenario);
  }
  return true;
}

bool EspScenarioStorage::clear() {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    return true;
  }
  const bool ok = preferences.clear();
  preferences.end();
  return ok;
}

void EspScenarioStorage::onSave(const Scenario & /*scenario*/) {
  saveAll(repository.all());
}

void EspScenarioStorage::onUpdate(const Scenario & /*oldScenario*/,
                                  const Scenario & /*newScenario*/) {
  saveAll(repository.all());
}

void EspScenarioStorage::onRemove(const Scenario & /*scenario*/) {
  saveAll(repository.all());
}
