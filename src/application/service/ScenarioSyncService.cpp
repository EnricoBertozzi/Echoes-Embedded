#include "application/service/ScenarioSyncService.hpp"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace {
constexpr size_t MAX_AUDIO_BYTES = 1048576; // 1024 KiB per contract
} // namespace

ScenarioSyncService::ScenarioSyncService(DeviceAuthenticator &authenticator,
                                         const char *apiBaseUrl,
                                         const char *hardwareId)
    : authenticator(authenticator), apiBaseUrl(apiBaseUrl ? apiBaseUrl : ""),
      hardwareId(hardwareId ? hardwareId : "") {}

SensorScenarioAssignment
ScenarioSyncService::getAssignment(uint8_t sensorIndex1Based) const {
  if (sensorIndex1Based >= 1 && sensorIndex1Based <= 2) {
    return assignments_[sensorIndex1Based - 1];
  }
  return {};
}

bool ScenarioSyncService::ensureAudioDir() {
  if (!LittleFS.exists("/audio")) {
    return LittleFS.mkdir("/audio");
  }
  return true;
}

size_t ScenarioSyncService::littleFsFreeBytes() const {
  // totalBytes() and usedBytes() are available in ESP32 LittleFS
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  if (total <= used)
    return 0;
  return total - used;
}

void ScenarioSyncService::cleanupOrphanAudioFiles() {
  // Collect used scenario ids
  std::vector<String> used;
  for (int i = 0; i < 2; i++) {
    if (assignments_[i].has && assignments_[i].scenarioId.length() > 0) {
      used.push_back(assignments_[i].scenarioId);
    }
  }
  File root = LittleFS.open("/audio");
  if (!root || !root.isDirectory()) {
    return;
  }
  File f = root.openNextFile();
  while (f) {
    String name = String(f.name());
    // name may be "/audio/xxx.mp3" or "xxx.mp3" depending on impl
    String base = name;
    int slash = base.lastIndexOf('/');
    if (slash >= 0)
      base = base.substring(slash + 1);
    bool keep = false;
    for (const auto &id : used) {
      String expected = id + ".mp3";
      if (base == expected) {
        keep = true;
        break;
      }
    }
    f.close();
    if (!keep) {
      String fullPath = name;
      if (!fullPath.startsWith("/audio/") && !fullPath.startsWith("/"))
        fullPath = "/audio/" + name;
      if (fullPath.startsWith("/audio/"))
        LittleFS.remove(fullPath);
    }
    f = root.openNextFile();
  }
}

bool ScenarioSyncService::downloadAndStoreAudio(
    const String &audioUrlPath, const String &scenarioId,
    const String & /*audioRevision*/) {
  if (!ensureAudioDir()) {
    Serial.println("[ScenarioSync] cannot create /audio dir");
    return false;
  }
  String url = apiBaseUrl + audioUrlPath;
  String token = String(authenticator.issueToken().c_str());
  if (token.length() == 0) {
    Serial.println("[ScenarioSync] no token (clock not synced?)");
    return false;
  }

  HTTPClient http;
  WiFiClient client;
  bool httpOk = http.begin(client, url);
  if (!httpOk) {
    Serial.println("[ScenarioSync] http.begin failed");
    return false;
  }
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("Accept", "audio/mpeg");

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[ScenarioSync] audio GET %d\n", code);
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len <= 0) {
    Serial.println("[ScenarioSync] audio size unknown or empty");
    http.end();
    return false;
  }
  if ((size_t)len > MAX_AUDIO_BYTES) {
    Serial.println("[ScenarioSync] audio too large");
    http.end();
    return false;
  }
  if (littleFsFreeBytes() < (size_t)len + 4096) { // reserve some slack
    Serial.println("[ScenarioSync] not enough LittleFS space");
    http.end();
    return false;
  }

  String finalPath = "/audio/" + scenarioId + ".mp3";
  String tmpPath = finalPath + ".tmp";
  File f = LittleFS.open(tmpPath, "w");
  if (!f) {
    Serial.println("[ScenarioSync] cannot open tmp file");
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t written = 0;
  uint8_t buf[512];
  while (http.connected() && (len > 0 || len == -1)) {
    size_t avail = stream->available();
    if (avail) {
      int toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
      int r = stream->readBytes(buf, toRead);
      if (r > 0) {
        f.write(buf, r);
        written += r;
        if (len > 0)
          len -= r;
        if (written > MAX_AUDIO_BYTES) {
          break;
        }
      }
    } else {
      delay(1);
    }
    if (written >= MAX_AUDIO_BYTES && len != 0)
      break;
  }
  f.close();
  http.end();

  if (written == 0 || written > MAX_AUDIO_BYTES) {
    LittleFS.remove(tmpPath);
    Serial.println("[ScenarioSync] download failed/too large");
    return false;
  }
  // Replace old
  LittleFS.remove(finalPath);
  if (!LittleFS.rename(tmpPath, finalPath)) {
    LittleFS.remove(tmpPath);
    Serial.println("[ScenarioSync] rename failed");
    return false;
  }
  Serial.printf("[ScenarioSync] saved %s (%u bytes)\n", finalPath.c_str(),
                (unsigned)written);
  return true;
}

bool ScenarioSyncService::sync() {
  String token = String(authenticator.issueToken().c_str());
  if (token.length() == 0) {
    Serial.println("[ScenarioSync] cannot issue token");
    return false;
  }
  String url = apiBaseUrl + "/api/v1/devices/me/scenario";
  HTTPClient http;
  WiFiClient client;
  bool okBegin = http.begin(client, url);
  if (!okBegin) {
    Serial.println("[ScenarioSync] manifest http.begin failed");
    return false;
  }
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code == 401) {
    Serial.println("[ScenarioSync] 401 unauthorized");
    http.end();
    return false;
  }
  if (code == 403) {
    Serial.println("[ScenarioSync] 403 forbidden");
    http.end();
    return false;
  }
  if (code != 200) {
    Serial.printf("[ScenarioSync] manifest GET %d\n", code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[ScenarioSync] JSON parse error: %s\n", err.c_str());
    return false;
  }
  JsonArray points = doc["points"].as<JsonArray>();
  // reset assignments
  for (int i = 0; i < 2; i++)
    assignments_[i] = SensorScenarioAssignment{};

  if (!points.isNull()) {
    for (JsonObject p : points) {
      int sensorIndex = p["sensorIndex"] | 0;
      if (sensorIndex != 1 && sensorIndex != 2)
        continue;
      JsonObject scenario = p["scenario"];
      if (scenario.isNull())
        continue;
      const char *sid = scenario["id"] | "";
      const char *audioUri = scenario["audioUri"] | "";
      const char *audioRev = scenario["audioRevision"] | "";
      if (strlen(sid) == 0 || strlen(audioUri) == 0)
        continue;
      String scenarioId(sid);
      String audioPath = "/audio/" + scenarioId + ".mp3";
      // Check if already cached with same revision
      bool needDownload = true;
      if (assignments_[sensorIndex - 1].has &&
          assignments_[sensorIndex - 1].scenarioId == scenarioId &&
          assignments_[sensorIndex - 1].audioRevision == String(audioRev) &&
          LittleFS.exists(audioPath)) {
        needDownload = false;
      }
      // Also check file exists even if revision not tracked
      if (LittleFS.exists(audioPath)) {
        // If revision matches cached, skip; but we just reset, so check new
        // assignment
      }
      if (needDownload) {
        if (!downloadAndStoreAudio(String(audioUri), scenarioId,
                                   String(audioRev))) {
          Serial.printf("[ScenarioSync] failed to download audio for %s\n",
                        sid);
          continue; // skip this assignment (keeps previous? no, we reset)
        }
      }
      assignments_[sensorIndex - 1].has = true;
      assignments_[sensorIndex - 1].scenarioId = scenarioId;
      assignments_[sensorIndex - 1].audioPath = audioPath;
      assignments_[sensorIndex - 1].audioRevision = String(audioRev);
    }
  }
  cleanupOrphanAudioFiles();
  postEvent("CONNECTION", String("null"));
  return true;
}

void ScenarioSyncService::postEvent(const char *type,
                                    const String &scenarioIdOrNullJson) {
  String token = String(authenticator.issueToken().c_str());
  if (token.length() == 0)
    return;
  String url = apiBaseUrl + "/api/v1/devices/me/events";
  HTTPClient http;
  {
    WiFiClient client;
    http.begin(client, url);
  }
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("Content-Type", "application/json");
  String body;
  if (scenarioIdOrNullJson == "null") {
    body = String("{\"type\":\"") + type + "\",\"scenarioId\":null}";
  } else {
    body = String("{\"type\":\"") + type + "\",\"scenarioId\":\"" +
           scenarioIdOrNullJson + "\"}";
  }
  int code = http.POST((uint8_t *)body.c_str(), body.length());
  if (code != 202) {
    Serial.printf("[ScenarioSync] event %s -> %d\n", type, code);
  }
  http.end();
}
