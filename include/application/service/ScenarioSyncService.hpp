
#pragma once

#include "infra/security/DeviceAuthenticator.hpp"

#include <Arduino.h>
#include <LittleFS.h>

#include <mutex>

struct SensorScenarioAssignment {
    bool has = false;
    String scenarioId;
    String audioPath;
    String audioRevision;
};

class ScenarioSyncService {
public:
    ScenarioSyncService(
        DeviceAuthenticator &authenticator,
        const char *apiBaseUrl,
        const char *hardwareId
    );

    // Restaura as associações persistidas na NVS.
    bool loadPersistedAssignments();

    // Consulta configuração e baixa arquivos ausentes.
    bool sync();

    // Retorna a associação de FSR 1 ou FSR 2.
    SensorScenarioAssignment getAssignment(
        uint8_t sensorIndex1Based
    ) const;

    // Falha de rede ou resposta diferente de 204 bloqueia áudio.
    bool authorizePlayback();

    // Envia eventos para o backend, quando utilizado.
    void postEvent(
        const char *type,
        const String &scenarioIdOrNullJson
    );

private:
    DeviceAuthenticator &authenticator;

    String apiBaseUrl;
    String hardwareId;

    mutable std::mutex assignmentsMutex;

    SensorScenarioAssignment assignments_[2];

    bool ensureAudioDir();

    bool persistAssignments(
        const SensorScenarioAssignment next[2]
    );

    bool downloadAndStoreAudio(
        const String &audioUrlPath,
        const String &scenarioId,
        const String &audioRevision,
        const String &destinationPath
    );

    size_t littleFsFreeBytes() const;
};

