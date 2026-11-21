
#include "application/service/ScenarioSyncService.hpp"
#include "Config.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <string>

#ifndef API_ALLOW_PLAIN_HTTP
// HTTP puro só para laboratório isolado; em produção deixe 0 (S3).
#define API_ALLOW_PLAIN_HTTP 0
#endif

namespace {

// Só aceita HTTPS, salvo HTTP explicitamente liberado para laboratório.
bool isAllowedApiUrl(const String &url) {
#if API_ALLOW_PLAIN_HTTP
    if (url.startsWith("http://")) {
        return true;
    }
#endif
    return url.startsWith("https://");
}

// Abre a requisição. HTTPS valida o certificado do servidor contra as CAs
// do bundle; HTTP só é usado quando liberado acima.
bool beginRequest(
    HTTPClient &http,
    const String &url,
    WiFiClient &plain,
    WiFiClientSecure &secure,
    uint32_t timeoutMs
) {
    http.setTimeout(timeoutMs);

    if (url.startsWith("https://")) {
        // bundle de CAs públicas do IDF, o mesmo que o MQTT usa (S3)
        secure.useBuiltinCACertBundle();
        return http.begin(secure, url);
    }

    return http.begin(plain, url);
}

constexpr size_t MAX_AUDIO_BYTES = 512 * 1024;
constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;
constexpr uint32_t CHUNK_TIMEOUT_MS = 5000;

constexpr char NVS_NAMESPACE[] = "echoes_sync";
constexpr char NVS_KEY[] = "manifest";

bool isUuid(const String &value) {
    if (value.length() != 36) {
        return false;
    }

    for (int i = 0; i < 36; ++i) {
        const char c = value[i];

        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                return false;
            }
        } else if (!isxdigit(
            static_cast<unsigned char>(c)
        )) {
            return false;
        }
    }

    return true;
}

// A nomeacao antiga usava 16+16 digitos hexadecimais e gerava
// um arquivo temporario de 41 caracteres; a criacao falha na placa.
// O hash abrange os UUIDs COMPLETOS do cenario e da revisao,
// permitindo um nome curto e estavel sem ignorar versoes.
// 14 hex (56 bits) + ".mp3.tmp" = 22 caracteres na pasta /audio.
String cacheAudioPath(
    const String &scenarioId,
    const String &revision
) {
    const String identity = scenarioId + ":" + revision;
    uint8_t digest[32] = {};

    if (mbedtls_sha256(
            reinterpret_cast<const unsigned char *>(identity.c_str()),
            identity.length(),
            digest,
            0
        ) != 0) {
        Serial.println("[ScenarioSync] Cache hash failed");
        return String();
    }

    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    char shortHash[15] = {};

    for (int i = 0; i < 7; ++i) {
        shortHash[i * 2] = HEX_DIGITS[digest[i] >> 4];
        shortHash[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0f];
    }

    return String("/audio/") + shortHash + ".mp3";
}

bool same(
    const SensorScenarioAssignment &a,
    const SensorScenarioAssignment &b
) {
    return a.has == b.has &&
           a.scenarioId == b.scenarioId &&
           a.audioPath == b.audioPath &&
           a.audioRevision == b.audioRevision;
}

} // namespace

ScenarioSyncService::ScenarioSyncService(
    DeviceAuthenticator &authenticator,
    const char *apiBaseUrl,
    const char *hardwareId
)
    : authenticator(authenticator),
      apiBaseUrl(apiBaseUrl ? apiBaseUrl : ""),
      hardwareId(hardwareId ? hardwareId : "") {

    while (this->apiBaseUrl.endsWith("/")) {
        this->apiBaseUrl.remove(
            this->apiBaseUrl.length() - 1
        );
    }
}

SensorScenarioAssignment
ScenarioSyncService::getAssignment(
    uint8_t sensorIndex1Based
) const {
    if (sensorIndex1Based < 1 ||
        sensorIndex1Based > 2) {
        return {};
    }

    std::lock_guard<std::mutex> lock(
        assignmentsMutex
    );

    return assignments_[sensorIndex1Based - 1];
}

bool ScenarioSyncService::ensureAudioDir() {
    if (!LittleFS.exists("/audio")) {
        if (!LittleFS.mkdir("/audio")) {
            Serial.println("[ScenarioSync] Could not create /audio");
            return false;
        }
    }

    File dir = LittleFS.open("/audio", "r");
    const bool valid = dir && dir.isDirectory();
    if (dir) {
        dir.close();
    }
    if (!valid) {
        Serial.println("[ScenarioSync] /audio is not a directory");
    }
    return valid;
}

size_t ScenarioSyncService::littleFsFreeBytes() const {
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();

    return total > used ? total - used : 0;
}

bool ScenarioSyncService::loadPersistedAssignments() {
    Preferences preferences;

    if (!preferences.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    String payload = preferences.getString(
        NVS_KEY, ""
    );

    preferences.end();

    if (payload.isEmpty()) {
        return false;
    }

    JsonDocument doc;

    if (deserializeJson(doc, payload) !=
        DeserializationError::Ok) {
        return false;
    }

    JsonArray entries = doc["points"].as<JsonArray>();

    if (entries.isNull() || entries.size() != 2) {
        return false;
    }

    SensorScenarioAssignment restored[2];

    for (int i = 0; i < 2; ++i) {
        JsonObject item =
            entries[i].as<JsonObject>();

        if (item.isNull()) {
            return false;
        }

        restored[i].has = item["has"] | false;

        restored[i].scenarioId =
            String(item["scenarioId"] | "");

        restored[i].audioPath =
            String(item["audioPath"] | "");

        restored[i].audioRevision =
            String(item["audioRevision"] | "");

        if (restored[i].has) {
            const bool valid =
                isUuid(restored[i].scenarioId) &&
                isUuid(restored[i].audioRevision) &&
                restored[i].audioPath.startsWith(
                    "/audio/"
                ) &&
                LittleFS.exists(restored[i].audioPath);

            if (!valid) {
                restored[i] =
                    SensorScenarioAssignment{};
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(
            assignmentsMutex
        );

        assignments_[0] = restored[0];
        assignments_[1] = restored[1];
    }

    Serial.println(
        "[ScenarioSync] Associations restored from NVS"
    );

    return true;
}

bool ScenarioSyncService::persistAssignments(
    const SensorScenarioAssignment next[2]
) {
    JsonDocument doc;

    JsonArray entries =
        doc["points"].to<JsonArray>();

    for (int i = 0; i < 2; ++i) {
        JsonObject item =
            entries.add<JsonObject>();

        item["has"] = next[i].has;
        item["scenarioId"] = next[i].scenarioId;
        item["audioPath"] = next[i].audioPath;
        item["audioRevision"] = next[i].audioRevision;
    }

    String payload;
    serializeJson(doc, payload);

    Preferences preferences;

    if (!preferences.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    const bool ok =
        preferences.putString(
            NVS_KEY,
            payload
        ) == payload.length();

    preferences.end();

    return ok;
}

bool ScenarioSyncService::downloadAndStoreAudio(
    const String &audioUrlPath,
    const String &scenarioId,
    const String &audioRevision,
    const String &destinationPath
) {
    (void)audioRevision;

    const String expectedPath =
        "/api/v1/scenarios/" +
        scenarioId +
        "/audio";

    if (!isUuid(scenarioId) ||
        audioUrlPath != expectedPath ||
        !ensureAudioDir()) {
        return false;
    }

    // Transporte: HTTPS (ou HTTP só em laboratório liberado).
    if (!isAllowedApiUrl(apiBaseUrl)) {
        Serial.println(
            "[ScenarioSync] Unsupported API transport"
        );
        return false;
    }

    String token = String(
        authenticator.issueToken().c_str()
    );

    if (token.isEmpty()) {
        Serial.println(
            "[ScenarioSync] Cannot generate device JWT"
        );
        return false;
    }

    WiFiClient plain;
    WiFiClientSecure secure;
    HTTPClient http;

    if (!beginRequest(
        http,
        apiBaseUrl + audioUrlPath,
        plain,
        secure,
        REQUEST_TIMEOUT_MS
    )) {
        return false;
    }

    http.addHeader(
        "Authorization",
        "Bearer " + token
    );

    http.addHeader(
        "Accept",
        "audio/mpeg"
    );

    const int status = http.GET();

    if (status != 200) {
        Serial.printf(
            "[ScenarioSync] Audio GET failed: %d\n",
            status
        );
        http.end();
        return false;
    }

    const int contentLen = http.getSize();

    if (contentLen <= 0 ||
        static_cast<size_t>(contentLen) >
            MAX_AUDIO_BYTES ||
        littleFsFreeBytes() <
            static_cast<size_t>(contentLen) + 8192) {

        Serial.println(
            "[ScenarioSync] Invalid audio size or insufficient storage"
        );

        http.end();
        return false;
    }

    const String temporaryPath =
        destinationPath + ".tmp";

    LittleFS.remove(temporaryPath);

    File file = LittleFS.open(
        temporaryPath,
        "w"
    );

    if (!file) {
        const int openError = errno;
        Serial.printf(
            "[ScenarioSync] Temp file open failed: %s "
            "(errno=%d, free=%u bytes)\n",
            temporaryPath.c_str(),
            openError,
            static_cast<unsigned>(littleFsFreeBytes())
        );
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();

    int remaining = contentLen;
    uint32_t lastProgress = millis();

    uint8_t buffer[1024];
    bool error = false;

    while (remaining > 0) {
        const int available = stream->available();

        if (available > 0) {
            const size_t toRead = std::min(
                static_cast<size_t>(remaining),
                std::min(
                    static_cast<size_t>(available),
                    sizeof(buffer)
                )
            );

            const int bytesRead =
                stream->readBytes(
                    buffer,
                    toRead
                );

            if (bytesRead <= 0 ||
                file.write(
                    buffer,
                    static_cast<size_t>(bytesRead)
                ) != static_cast<size_t>(bytesRead)) {

                error = true;
                break;
            }

            remaining -= bytesRead;
            lastProgress = millis();

        } else if (
            millis() - lastProgress > CHUNK_TIMEOUT_MS
        ) {
            error = true;
            break;

        } else {
            delay(2);
        }
    }

    file.flush();
    file.close();
    http.end();

    if (error || remaining != 0) {
        LittleFS.remove(temporaryPath);

        Serial.println(
            "[ScenarioSync] Incomplete MP3; cached audio preserved"
        );

        return false;
    }

    // O nome inclui a revisão. Não substitui o arquivo
    // que eventualmente esteja aberto pelo decoder.
    if (!LittleFS.rename(
        temporaryPath,
        destinationPath
    )) {
        LittleFS.remove(temporaryPath);
        return false;
    }

    Serial.printf(
        "[ScenarioSync] MP3 saved: %s (%d bytes)\n",
        destinationPath.c_str(),
        contentLen
    );

    return true;
}

bool ScenarioSyncService::sync() {
    if (WiFi.status() != WL_CONNECTED ||
        !isAllowedApiUrl(apiBaseUrl)) {
        return false;
    }

    String token = String(
        authenticator.issueToken().c_str()
    );

    if (token.isEmpty()) {
        return false;
    }

    WiFiClient plain;
    WiFiClientSecure secure;
    HTTPClient http;

    if (!beginRequest(
        http,
        apiBaseUrl + "/api/v1/devices/me/scenario",
        plain,
        secure,
        REQUEST_TIMEOUT_MS
    )) {
        return false;
    }

    http.addHeader(
        "Authorization",
        "Bearer " + token
    );

    http.addHeader(
        "Accept",
        "application/json"
    );

    const int code = http.GET();

    if (code == 401 || code == 403) {
        revokePlaybackAuthorization();
    }

    if (code != 200) {
        Serial.printf(
            "[ScenarioSync] Manifest GET: %d; cache preserved\n",
            code
        );

        http.end();
        return false;
    }

    // 200 no manifesto = dispositivo ativo e autorizado para reproduzir
    markPlaybackAuthorized();

    String payload = http.getString();
    http.end();

    JsonDocument doc;

    if (deserializeJson(doc, payload) !=
        DeserializationError::Ok) {
        Serial.println(
            "[ScenarioSync] Invalid manifest JSON"
        );
        return false;
    }

    JsonArray points = doc["points"].as<JsonArray>();

    if (points.isNull()) {
        return false;
    }

    SensorScenarioAssignment previous[2];

    {
        std::lock_guard<std::mutex> lock(
            assignmentsMutex
        );

        previous[0] = assignments_[0];
        previous[1] = assignments_[1];
    }

    // A configuração antiga continua válida em memória
    // até a nova ter seus arquivos disponíveis.
    SensorScenarioAssignment next[2];

    bool complete = true;
    bool visited[2] = {false, false};

    for (JsonObject point : points) {
        const int sensorIndex =
            point["sensorIndex"] | 0;

        if (sensorIndex < 1 || sensorIndex > 2) {
            continue;
        }

        const int index = sensorIndex - 1;

        if (visited[index]) {
            complete = false;
            continue;
        }

        visited[index] = true;

        JsonObject scenario =
            point["scenario"].as<JsonObject>();

        // Ponto sem cenário: fica sem atribuição.
        if (scenario.isNull()) {
            continue;
        }

        const String scenarioId =
            String(scenario["id"] | "");

        const String audioUri =
            String(scenario["audioUri"] | "");

        const String revision =
            String(scenario["audioRevision"] | "");

        const String expectedUri =
            "/api/v1/scenarios/" +
            scenarioId +
            "/audio";

        if (!isUuid(scenarioId) ||
            !isUuid(revision) ||
            audioUri != expectedUri) {
            complete = false;

            next[index] = previous[index];
            continue;
        }

        const String localPath = cacheAudioPath(
            scenarioId,
            revision
        );

        if (localPath.isEmpty()) {
            complete = false;
            next[index] = previous[index];
            continue;
        }

        bool cached = LittleFS.exists(localPath);

        if (cached) {
            File file = LittleFS.open(
                localPath,
                "r"
            );

            cached = file &&
                file.size() > 0;

            if (file) {
                file.close();
            }
        }

        if (!cached) {
            const bool downloaded =
                downloadAndStoreAudio(
                    audioUri,
                    scenarioId,
                    revision,
                    localPath
                );

            if (!downloaded) {
                next[index] = previous[index];
                complete = false;
                continue;
            }
        }

        next[index].has = true;
        next[index].scenarioId = scenarioId;
        next[index].audioPath = localPath;
        next[index].audioRevision = revision;
    }

    const bool changed =
        !same(next[0], previous[0]) ||
        !same(next[1], previous[1]);

    if (changed && !persistAssignments(next)) {
        Serial.println(
            "[ScenarioSync] Failed to persist assignments"
        );
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(
            assignmentsMutex
        );

        assignments_[0] = next[0];
        assignments_[1] = next[1];
    }

    if (complete) {
        Serial.println(
            "[ScenarioSync] Synchronization completed"
        );
    }

    return complete;
}

void ScenarioSyncService::markPlaybackAuthorized() {
    playbackAuthAtMs_.store(millis());
    playbackAuthSeen_.store(true);
}

void ScenarioSyncService::revokePlaybackAuthorization() {
    playbackAuthSeen_.store(false);
}

bool ScenarioSyncService::playbackAllowed() const {
    if (!playbackAuthSeen_.load()) {
        return false;
    }

    return (millis() - playbackAuthAtMs_.load()) <
        PLAYBACK_AUTH_TTL_MS;
}

void ScenarioSyncService::postEvent(
    const char *type,
    const String &scenarioIdOrNullJson
) {
    if (WiFi.status() != WL_CONNECTED ||
        !isAllowedApiUrl(apiBaseUrl)) {
        return;
    }

    String token = String(
        authenticator.issueToken().c_str()
    );

    if (token.isEmpty()) {
        return;
    }

    WiFiClient plain;
    WiFiClientSecure secure;
    HTTPClient http;

    if (!beginRequest(
        http,
        apiBaseUrl + "/api/v1/devices/me/events",
        plain,
        secure,
        1800
    )) {
        return;
    }

    http.addHeader(
        "Authorization",
        "Bearer " + token
    );

    http.addHeader(
        "Content-Type",
        "application/json"
    );

    const String scenarioValue =
        scenarioIdOrNullJson == "null"
            ? "null"
            : String("\"") +
              scenarioIdOrNullJson +
              "\"";

    const String body =
        String("{\"type\":\"") +
        type +
        "\",\"scenarioId\":" +
        scenarioValue +
        "}";

    const int code = http.POST(
        reinterpret_cast<uint8_t *>(
            const_cast<char *>(body.c_str())
        ),
        body.length()
    );

    if (code != 202) {
        Serial.printf(
            "[ScenarioSync] Event %s -> HTTP %d\n",
            type,
            code
        );
    }

    http.end();
}
