
#include "application/service/ScenarioSyncService.hpp"
#include "application/service/ApiUrl.hpp"
#include "application/service/RemoteLog.hpp"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <ctime>
#include <string>

namespace {

constexpr size_t STORAGE_RESERVE_BYTES = 8 * 1024;
constexpr size_t MAX_MANIFEST_BYTES = 16 * 1024;
constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;

constexpr char NVS_NAMESPACE[] = "echoes_sync";
constexpr char NVS_KEY[] = "manifest";
constexpr char AUTH_NAMESPACE[] = "echoes_play";
constexpr char AUTH_KEY[] = "authorization";

// Each request owns its client; IDF decodes chunked bodies and validates HTTPS with the bundle.
class ApiRequest {
public:
    ApiRequest(const String &url, const std::string &token, int timeoutMs)
        : url(url) {
        if (token.empty()) {
            return;
        }
        if (!isAllowedApiUrl(url)) {
            Serial.println("[DeviceAPI] Invalid API_BASE_URL: https:// required (http:// only with API_ALLOW_PLAIN_HTTP 1)");
            return;
        }

        esp_http_client_config_t config = {};
        config.url = this->url.c_str();
        config.timeout_ms = timeoutMs;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.disable_auto_redirect = true;
        client = esp_http_client_init(&config);
        if (!client) {
            Serial.println("[DeviceAPI] HTTP client initialization failed");
        } else {
            const std::string authorization = "Bearer " + token;
            if (!succeeded(esp_http_client_set_header(client, "Authorization",
                           authorization.c_str()), "authorization header") ||
                !succeeded(esp_http_client_set_header(client, "Accept-Encoding",
                           "identity"), "encoding header")) {
                esp_http_client_cleanup(client);
                client = nullptr;
            }
        }
    }

    ~ApiRequest() {
        if (client) {
            esp_http_client_cleanup(client);
        }
    }

    ApiRequest(const ApiRequest &) = delete;
    ApiRequest &operator=(const ApiRequest &) = delete;

    int open(const char *accept, const String *body = nullptr) {
        if (!client ||
            !succeeded(esp_http_client_set_header(client, "Accept", accept), "accept header") ||
            !succeeded(esp_http_client_set_method(client, body ? HTTP_METHOD_POST
                                                    : HTTP_METHOD_GET), "method")) {
            return -1;
        }
        if (body && !succeeded(esp_http_client_set_header(
                        client, "Content-Type", "application/json"), "content type")) {
            return -1;
        }
        if (!succeeded(esp_http_client_open(client, body ? body->length() : 0), "open")) {
            return -1;
        }
        if (body) {
            size_t sent = 0;
            while (sent < body->length()) {
                const int written = esp_http_client_write(
                    client, body->c_str() + sent, body->length() - sent);
                if (written <= 0) {
                    succeeded(ESP_FAIL, "request body");
                    return -1;
                }
                sent += written;
            }
        }
        if (esp_http_client_fetch_headers(client) < 0) {
            succeeded(ESP_FAIL, "response headers");
            return -1;
        }
        return esp_http_client_get_status_code(client);
    }

    int64_t length() const {
        return esp_http_client_get_content_length(client);
    }

    bool chunked() const {
        return esp_http_client_is_chunked_response(client);
    }

    bool complete() const {
        return esp_http_client_is_complete_data_received(client);
    }

    int read(char *buffer, size_t size) {
        return esp_http_client_read(client, buffer, size);
    }

    bool readManifest(String &payload) {
        if ((!chunked() && length() <= 0) ||
            length() > static_cast<int64_t>(MAX_MANIFEST_BYTES)) {
            return false;
        }
        char buffer[1024];
        while (true) {
            const int count = read(buffer, sizeof(buffer));
            if (count < 0) {
                return false;
            }
            if (count == 0) {
                return complete() && !payload.isEmpty() &&
                       (chunked() ||
                        payload.length() == static_cast<size_t>(length()));
            }
            if (payload.length() + count > MAX_MANIFEST_BYTES ||
                !payload.concat(buffer, count)) {
                return false;
            }
        }
    }

private:
    bool succeeded(esp_err_t result, const char *stage) const {
        if (result == ESP_OK) {
            return true;
        }
        Serial.printf("[DeviceAPI] %s failed: %s (errno=%d); no HTTP response\n",
                      stage, esp_err_to_name(result), esp_http_client_get_errno(client));
        return false;
    }

    const String url;
    esp_http_client_handle_t client = nullptr;
};

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

    ApiRequest http(apiBaseUrl + audioUrlPath,
                    authenticator.issueToken(), REQUEST_TIMEOUT_MS);
    const int status = http.open("audio/mpeg");

    if (status != 200) {
        Serial.printf(
            "[ScenarioSync] Audio GET failed: %d\n",
            status
        );
        REMOTE_LOG(Error, Sync, "AUDIO_DOWNLOAD_HTTP",
                   "Download do áudio falhou (HTTP %d) cenário %.8s. -1 = queda/timeout de rede; 404 = arquivo não existe na API.",
                   status, scenarioId.c_str());
        return false;
    }

    const String temporaryPath = destinationPath + ".tmp";
    LittleFS.remove(temporaryPath);

    const int64_t contentLen = http.length();
    const size_t freeBytes = littleFsFreeBytes();
    const size_t budget = freeBytes > STORAGE_RESERVE_BYTES
        ? freeBytes - STORAGE_RESERVE_BYTES : 0;

    if (budget == 0 || (!http.chunked() && contentLen <= 0) ||
        contentLen > static_cast<int64_t>(budget)) {
        Serial.println(
            "[ScenarioSync] Invalid audio size or insufficient storage"
        );
        REMOTE_LOG(Error, Sync, "AUDIO_NO_SPACE",
                   "Sem espaço ou tamanho inválido para o áudio %.8s (tam=%lld, livre=%u B). Remova cenários antigos ou reduza o MP3.",
                   scenarioId.c_str(), static_cast<long long>(contentLen),
                   static_cast<unsigned>(freeBytes));
        return false;
    }

    File file = LittleFS.open(
        temporaryPath,
        "w"
    );

    if (!file) {
        const int openError = errno;
        REMOTE_LOG(Error, System, "FS_OPEN_FAIL",
                   "Não foi possível criar arquivo temporário na LittleFS (errno=%d). Sistema de arquivos cheio ou corrompido.",
                   openError);
        Serial.printf(
            "[ScenarioSync] Temp file open failed: %s "
            "(errno=%d, free=%u bytes)\n",
            temporaryPath.c_str(),
            openError,
            static_cast<unsigned>(littleFsFreeBytes())
        );
        return false;
    }

    size_t received = 0;
    uint8_t buffer[1024];
    bool error = false;

    while (true) {
        // A zero read is only success when IDF saw the terminal chunk.
        const int count = http.read(
            reinterpret_cast<char *>(buffer), sizeof(buffer));
        if (count < 0) {
            error = true;
            break;
        }
        if (count == 0) {
            break;
        }
        const size_t bytesRead = static_cast<size_t>(count);
        if (bytesRead > budget - received ||
            littleFsFreeBytes() < STORAGE_RESERVE_BYTES + bytesRead ||
            file.write(buffer, bytesRead) != bytesRead) {
            error = true;
            break;
        }
        received += bytesRead;
        // Let the decoder run while flash/network I/O is progressing.
        delay(1);
    }

    file.flush();
    error = error || file.getWriteError() || file.size() != received ||
            littleFsFreeBytes() < STORAGE_RESERVE_BYTES;
    file.close();

    if (error || received == 0 || !http.complete() ||
        (!http.chunked() && received != static_cast<uint64_t>(contentLen))) {
        LittleFS.remove(temporaryPath);

        Serial.println(
            "[ScenarioSync] Incomplete MP3; cached audio preserved"
        );
        REMOTE_LOG(Error, Sync, "AUDIO_INCOMPLETE",
                   "Download do MP3 %.8s incompleto (%u B recebidos). Conexão caiu ou flash cheia; o áudio anterior foi mantido.",
                   scenarioId.c_str(), static_cast<unsigned>(received));

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
        "[ScenarioSync] MP3 saved: %s (%u bytes)\n",
        destinationPath.c_str(),
        static_cast<unsigned>(received)
    );
    REMOTE_LOG(Info, Sync, "AUDIO_DOWNLOADED",
               "Novo áudio baixado: cenário %.8s, %u bytes.",
               scenarioId.c_str(), static_cast<unsigned>(received));

    return true;
}

bool ScenarioSyncService::sync() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    String payload;
    {
        ApiRequest http(apiBaseUrl + "/api/v1/devices/me/scenario",
                        authenticator.issueToken(), REQUEST_TIMEOUT_MS);
        const int code = http.open("application/json");

        if (code == 401 || code == 403) {
            updatePlaybackAuthorization(false);
            REMOTE_LOG(Error, Sync, "MANIFEST_DENIED",
                       "API recusou o dispositivo (HTTP %d). Token inválido, relógio errado ou dispositivo bloqueado/não vinculado à instituição.", code);
        } else if (code < 0) {
            REMOTE_LOG(Error, Net, "MANIFEST_NO_RESPONSE",
                       "Sem resposta da API ao buscar cenários. Verifique internet, DNS, certificado TLS e API_BASE_URL. Heap livre=%lu.",
                       static_cast<unsigned long>(ESP.getFreeHeap()));
        } else if (code != 200) {
            REMOTE_LOG(Warn, Sync, "MANIFEST_HTTP_ERROR",
                       "API respondeu HTTP %d ao buscar cenários. 5xx = falha no servidor; 404 = dispositivo/rota inexistente.", code);
        }
        if (code < 0) {
            Serial.println("[ScenarioSync] Manifest not received; see authentication/transport diagnostics");
        } else {
            Serial.printf("[ScenarioSync] Manifest GET: %d\n", code);
        }
        if (code != 200 || !http.readManifest(payload)) {
            return false;
        }
    }

    JsonDocument doc;

    if (deserializeJson(doc, payload) !=
        DeserializationError::Ok) {
        Serial.println(
            "[ScenarioSync] Invalid manifest JSON"
        );
        REMOTE_LOG(Error, Sync, "MANIFEST_INVALID",
                   "Manifesto de cenários com JSON inválido ou truncado. Provável falha na API ou conexão instável.");
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

    // Publish only after every download succeeds; complete revisions stay cached.
    if (!complete) {
        return false;
    }

    const bool changed =
        !same(next[0], previous[0]) ||
        !same(next[1], previous[1]);

    if (changed && !persistAssignments(next)) {
        Serial.println(
            "[ScenarioSync] Failed to persist assignments"
        );
        REMOTE_LOG(Error, System, "NVS_WRITE_FAIL",
                   "Não foi possível salvar os cenários na NVS. Após reiniciar o dispositivo pode perder a associação dos sensores.");
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
        if (changed) {
            REMOTE_LOG(Info, Sync, "SCENARIOS_UPDATED",
                       "Cenários atualizados: FSR1=%.8s, FSR2=%.8s.",
                       next[0].has ? next[0].scenarioId.c_str() : "-",
                       next[1].has ? next[1].scenarioId.c_str() : "-");
        }
    }

    return complete;
}

bool ScenarioSyncService::loadPlaybackAuthorization() {
    playbackAuthorized_.store(false);
    playbackAuthorizationDirty_ = false;
    Preferences preferences;
    if (!preferences.begin(AUTH_NAMESPACE, true)) {
        return false;
    }
    const String payload = preferences.getString(AUTH_KEY, "");
    preferences.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok ||
        !doc["allowed"].is<bool>() ||
        String(doc["hardwareId"] | "") != hardwareId ||
        String(doc["apiBaseUrl"] | "") != apiBaseUrl) {
        return false;
    }
    playbackAuthorized_.store(doc["allowed"].as<bool>());
    Serial.printf("[PlaybackAuth] Restored from NVS: %s\n",
                  isPlaybackAuthorized() ? "allowed" : "denied");
    return true;
}

void ScenarioSyncService::updatePlaybackAuthorization(bool allowed) {
    const bool previous = playbackAuthorized_.exchange(allowed);
    playbackAuthorizationDirty_ = playbackAuthorizationDirty_ || previous != allowed;
    if (!playbackAuthorizationDirty_) {
        return;
    }

    JsonDocument doc;
    doc["allowed"] = allowed;
    doc["hardwareId"] = hardwareId;
    doc["apiBaseUrl"] = apiBaseUrl;
    String payload;
    serializeJson(doc, payload);

    Preferences preferences;
    if (preferences.begin(AUTH_NAMESPACE, false)) {
        playbackAuthorizationDirty_ =
            preferences.putString(AUTH_KEY, payload) != payload.length();
        // A failed revoke must not leave a usable grant if the old record can be removed.
        if (playbackAuthorizationDirty_ && !allowed && preferences.remove(AUTH_KEY)) {
            playbackAuthorizationDirty_ = false;
        }
        preferences.end();
    }
    if (playbackAuthorizationDirty_) {
        Serial.println("[PlaybackAuth] Could not persist decision; will retry on next response");
    }
}

void ScenarioSyncService::refreshPlaybackAuthorization() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    ApiRequest http(apiBaseUrl + "/api/v1/devices/me/playback-authorization",
                    authenticator.issueToken(), 1800);
    const int code = http.open("application/json");
    if (code == 204) {
        const bool was = isPlaybackAuthorized();
        updatePlaybackAuthorization(true);
        if (!was) {
            REMOTE_LOG(Info, Sync, "PLAYBACK_AUTHORIZED",
                       "Reprodução autorizada pela API (inclusive offline até a próxima revisão).");
        }
        Serial.println("[PlaybackAuth] HTTP 204: playback allowed, including offline");
    } else if (code == 401 || code == 403) {
        updatePlaybackAuthorization(false);
        REMOTE_LOG(Warn, Sync, "PLAYBACK_DENIED",
                   "API negou a reprodução (HTTP %d). Assinatura/licença da instituição inativa ou dispositivo bloqueado; o áudio não toca.", code);
        Serial.printf("[PlaybackAuth] HTTP %d: playback denied\n", code);
    } else {
        REMOTE_LOG(Warn, Net, "AUTH_CHECK_UNAVAILABLE",
                   "Não foi possível verificar a autorização (%d). Mantida a decisão anterior (%s); provável falha de rede/servidor.",
                   code, isPlaybackAuthorized() ? "permitido" : "negado");
        Serial.printf("[PlaybackAuth] Check unavailable (%d); keeping previous decision: %s\n",
                      code, isPlaybackAuthorized() ? "allowed" : "denied");
    }
}

bool ScenarioSyncService::flushLogs() {
    constexpr size_t BATCH = 12;

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    RemoteLog &remote = RemoteLog::instance();
    LogEntry batch[BATCH];
    uint32_t dropped = 0;
    const size_t count = remote.peekBatch(batch, BATCH, dropped);
    if (count == 0) {
        return true;
    }

    JsonDocument doc;
    doc["bootId"] = remote.bootId();
    doc["up"] = static_cast<uint32_t>(millis());
    doc["dropped"] = dropped;
    JsonArray events = doc["events"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i) {
        JsonObject item = events.add<JsonObject>();
        item["seq"] = batch[i].seq;
        item["up"] = batch[i].uptimeMs;
        if (batch[i].epoch != 0) {
            char iso[24];
            const time_t epoch = static_cast<time_t>(batch[i].epoch);
            struct tm utc;
            gmtime_r(&epoch, &utc);
            strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &utc);
            item["ts"] = iso;
        }
        item["lvl"] = RemoteLog::levelName(batch[i].level);
        item["cat"] = RemoteLog::categoryName(batch[i].category);
        item["code"] = batch[i].code;
        item["msg"] = batch[i].message;
        if (batch[i].count > 1) {
            item["n"] = batch[i].count;
        }
    }
    String body;
    serializeJson(doc, body);

    ApiRequest http(apiBaseUrl + "/api/v1/devices/me/logs",
                    authenticator.issueToken(), REQUEST_TIMEOUT_MS);
    const int code = http.open("application/json", &body);

    // Falhas daqui só vão para a Serial: registrar remotamente criaria um laço.
    if (code == 202) {
        remote.commit(batch[count - 1].seq, dropped);
        return true;
    }
    if (code == 400 || code == 413 || code == 422) {
        // Lote que a API nunca aceitará; descarta para não travar a fila.
        Serial.printf("[RemoteLog] Batch rejected (HTTP %d); discarded\n", code);
        remote.discard(batch[count - 1].seq, dropped);
        return true;
    }
    Serial.printf("[RemoteLog] Flush failed (HTTP %d); will retry\n", code);
    return false;
}

bool ScenarioSyncService::postEvent(
    const char *type,
    const String &scenarioId
) {
    if (WiFi.status() != WL_CONNECTED || type == nullptr) {
        return false;
    }

    const bool connection = std::strcmp(type, "CONNECTION") == 0;
    if (connection ? !scenarioId.isEmpty()
                   : ((std::strcmp(type, "PLAY") != 0 &&
                       std::strcmp(type, "STOP") != 0) || !isUuid(scenarioId))) {
        return false;
    }

    JsonDocument doc;
    doc["type"] = type;
    if (connection) {
        doc["scenarioId"] = nullptr;
    } else {
        doc["scenarioId"] = scenarioId;
    }
    String body;
    serializeJson(doc, body);

    ApiRequest http(apiBaseUrl + "/api/v1/devices/me/events",
                    authenticator.issueToken(), 1800);
    const int code = http.open("application/json", &body);
    if (code < 0) {
        REMOTE_LOG(Warn, Net, "EVENT_NO_RESPONSE",
                   "Evento %s não enviado: sem resposta da API (heap livre=%lu, maior bloco=%lu). Falta de memória para TLS ou rede instável.",
                   type, static_cast<unsigned long>(ESP.getFreeHeap()),
                   static_cast<unsigned long>(ESP.getMaxAllocHeap()));
        Serial.printf("[ScenarioSync] Event %s not accepted; no HTTP response (heap livre=%lu, maior bloco=%lu, minimo=%lu)\n",
                      type, static_cast<unsigned long>(ESP.getFreeHeap()),
                      static_cast<unsigned long>(ESP.getMaxAllocHeap()),
                      static_cast<unsigned long>(ESP.getMinFreeHeap()));
    } else {
        Serial.printf("[ScenarioSync] Event %s -> HTTP %d\n", type, code);
    }
    return code == 202;
}
