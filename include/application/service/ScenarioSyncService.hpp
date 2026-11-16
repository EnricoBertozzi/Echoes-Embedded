#pragma once

#include "infra/security/DeviceAuthenticator.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include <string>
#include <vector>

struct SensorScenarioAssignment {
  bool has = false;
  String scenarioId;
  String audioPath; // e.g. /audio/<id>.mp3
  String audioRevision;
};

class ScenarioSyncService {
public:
  ScenarioSyncService(DeviceAuthenticator &authenticator,
                      const char *apiBaseUrl,
                      const char *hardwareId);

  // Fetch manifest, download missing/outdated audio files, update assignments.
  bool sync();

  SensorScenarioAssignment getAssignment(uint8_t sensorIndex1Based) const;

  // Post event to backend; best effort.
  void postEvent(const char *type, const String &scenarioIdOrNullJson);

private:
  DeviceAuthenticator &authenticator;
  String apiBaseUrl;
  String hardwareId;

  SensorScenarioAssignment assignments_[2]; // index 0 -> sensor 1

  bool ensureAudioDir();
  bool downloadAndStoreAudio(const String &audioUrlPath,
                             const String &scenarioId,
                             const String &audioRevision);
  void cleanupOrphanAudioFiles();
  size_t littleFsFreeBytes() const;
};
