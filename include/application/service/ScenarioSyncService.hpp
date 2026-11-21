
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

    // Autorização de reprodução local (B2/RNF03): vale por 24 h após o último
    // sync bem-sucedido e não faz HTTP. Sem rede o áudio continua tocando
    // dentro dessa janela; 401/403 do backend a revogam imediatamente.
    bool playbackAllowed() const;

    // Envia eventos para o backend, quando utilizado.
    void postEvent(
        const char *type,
        const String &scenarioIdOrNullJson
    );

private:
    static constexpr unsigned long PLAYBACK_AUTH_TTL_MS =
        24UL * 60UL * 60UL * 1000UL;

    void markPlaybackAuthorized();
    void revokePlaybackAuthorization();

    std::atomic<bool> playbackAuthSeen_{false};
    std::atomic<unsigned long> playbackAuthAtMs_{0};

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

