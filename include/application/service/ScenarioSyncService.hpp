
#pragma once

#include "infra/security/DeviceAuthenticator.hpp"

#include <Arduino.h>
#include <LittleFS.h>

#include <atomic>
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

    bool loadPlaybackAuthorization();

    // Consulta local; nunca faz HTTP ao pressionar um sensor.
    bool isPlaybackAuthorized() const { return playbackAuthorized_.load(); }

    // A task de rede atualiza a permissão; falhas temporárias preservam a decisão anterior.
    void refreshPlaybackAuthorization();

    // Chamado pela task de rede; true somente quando a API aceita (202).
    bool postEvent(
        const char *type,
        const String &scenarioId = String()
    );

private:
    DeviceAuthenticator &authenticator;

    String apiBaseUrl;
    String hardwareId;

    mutable std::mutex assignmentsMutex;

    SensorScenarioAssignment assignments_[2];
    std::atomic<bool> playbackAuthorized_{false};
    bool playbackAuthorizationDirty_ = false;

    void updatePlaybackAuthorization(bool allowed);

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
