#include "application/security/JwtBuilder.hpp"
#include "application/service/PlaybackEventQueue.hpp"
#include "application/service/ScenarioSyncService.hpp"
#include "infra/hardware/EspAudioPlayer.hpp"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <cassert>
#include <iostream>
#include <limits>

struct EspWifiAP {
    int countConnect() const { return 0; }
};
#include "controls-production.inc"

static time_t clockNow = 1795176000;
static unsigned clockReads = 0;
extern "C" time_t __wrap_time(time_t *out) {
    ++clockReads;
    if (out) *out = clockNow;
    return clockNow;
}

class TestKeys : public DeviceKeys {
public:
    EVP_PKEY *key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1");
    bool loaded = true, signSucceeds = true;
    ~TestKeys() override { EVP_PKEY_free(key); }
    bool load() override { return loaded; }
    bool create() override { return false; }
    bool store() override { return false; }
    void reset() override { loaded = false; }
    bool isLoaded() const override { return loaded; }
    std::string publicKeyPem() override {
        BIO *bio = BIO_new(BIO_s_mem());
        assert(PEM_write_bio_PUBKEY(bio, key) == 1);
        char *data;
        const long length = BIO_get_mem_data(bio, &data);
        std::string pem(data, length);
        BIO_free(bio);
        return pem;
    }
    bool sign(const std::string &message, std::string &der) override {
        if (!signSucceeds) return false;
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        assert(EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1);
        size_t length = 0;
        assert(EVP_DigestSign(ctx, nullptr, &length,
            reinterpret_cast<const unsigned char *>(message.data()), message.size()) == 1);
        der.resize(length);
        assert(EVP_DigestSign(ctx, reinterpret_cast<unsigned char *>(der.data()), &length,
            reinterpret_cast<const unsigned char *>(message.data()), message.size()) == 1);
        der.resize(length);
        EVP_MD_CTX_free(ctx);
        return true;
    }
};

constexpr char ID[] = "12345678-1234-1234-1234-123456789abc";
constexpr char REV1[] = "aaaaaaaa-1111-2222-3333-123456789abc";
constexpr char REV2[] = "bbbbbbbb-1111-2222-3333-123456789abc";
const std::string AUDIO = std::string("/api/v1/scenarios/") + ID + "/audio";
constexpr char MANIFEST[] = "/api/v1/devices/me/scenario";

String manifest(const char *revision) {
    return String("{\"deviceId\":\"test\",\"points\":[{\"sensorIndex\":1,\"scenario\":"
        "{\"id\":\"") + ID + "\",\"audioUri\":\"" + AUDIO +
        "\",\"audioRevision\":\"" + revision + "\"}}]}";
}

void resetFakes() {
    assert(liveHttpClients == 0);
    httpReplies.clear();
    httpCalls.clear();
    LittleFS = {};
    Preferences::values.clear();
    Preferences::failWrite = false;
    Preferences::writes = 0;
    File::writeLimit = std::numeric_limits<size_t>::max();
    File::flushError = false;
    WiFi.state = WL_CONNECTED;
    Serial.output.clear();
}

void jwtTest(TestKeys &keys, DeviceAuthenticator &auth) {
    keys.loaded = false;
    assert(auth.issueToken().empty());
    keys.loaded = true;
    clockNow = 1000;
    assert(auth.issueToken().empty());
    clockNow = 1795176000;
    keys.signSucceeds = false;
    assert(auth.issueToken().empty());
    keys.signSucceeds = true;
    clockReads = 0;
    std::cout << "TOKEN=" << auth.issueToken() << '\n';
    assert(clockReads == 1);
    std::cout << "PUBLIC=" << JwtBuilder::base64Url(keys.publicKeyPem()) << '\n';
}

void preflightTest(TestKeys &keys, DeviceAuthenticator &auth) {
    resetFakes();
    ScenarioSyncService sync(auth, "https://lab.example", "MAC");
    for (int failure = 0; failure < 3; ++failure) {
        keys.loaded = failure != 0;
        clockNow = failure == 1 ? 1000 : 1795176000;
        keys.signSucceeds = failure != 2;
        Serial.output.clear();
        assert(!sync.sync() && !sync.postEvent("CONNECTION"));
        sync.refreshPlaybackAuthorization();
        assert(!sync.isPlaybackAuthorized());
        assert(httpCalls.empty() && liveHttpClients == 0);
        const char *reason[] = {"key not loaded", "clock not synchronized", "signing failed"};
        assert(Serial.output.find(reason[failure]) != std::string::npos);
        assert(Serial.output.find("HTTP -1") == std::string::npos);
    }
    keys.signSucceeds = true;
    ScenarioSyncService invalid(auth, "lab.example", "MAC");
    assert(!invalid.sync());
    assert(Serial.output.find("Invalid API_BASE_URL") != std::string::npos);
    assert(httpCalls.empty());

    for (bool openError : {true, false}) {
        Serial.output.clear();
        HttpReply reply{MANIFEST, 200, "{\"points\":[]}"};
        reply.openError = openError;
        reply.headerError = !openError;
        httpReplies.push_back(reply);
        assert(!sync.sync());
        assert(Serial.output.find(openError ? "open failed" : "response headers failed") != std::string::npos);
        assert(Serial.output.find("no HTTP response") != std::string::npos);
        assert(httpReplies.empty() && liveHttpClients == 0);
    }
    std::cout << "PASS: missing key/clock/signature skips HTTP; transport failures identify their stage\n";
}

void seed(ScenarioSyncService &sync) {
    httpReplies.push_back({MANIFEST, 200, manifest(REV1)});
    httpReplies.push_back({AUDIO, 200, "old audio"});
    assert(sync.sync());
    assert(sync.getAssignment(1).has);
    assert(!sync.getAssignment(2).has);
    assert(!sync.getAssignment(0).has);
    assert(!sync.getAssignment(3).has);
    assert(httpReplies.empty());
}

void downloadsTest(DeviceAuthenticator &auth) {
    // Both native-IDF decoded representations must support files >512 KiB.
    for (bool chunked : {false, true}) {
        resetFakes();
        ScenarioSyncService sync(auth, chunked ? "https://lab.example/" : "http://lab.example/", "MAC");
        seed(sync);
        const auto previous = sync.getAssignment(1);
        const std::string audio(600 * 1024, '\x55');
        httpReplies.push_back({MANIFEST, 200, manifest(REV2), -2, true});
        httpReplies.push_back({AUDIO, 200, audio, -2, chunked});
        assert(sync.sync());
        auto current = sync.getAssignment(1);
        assert(current.audioRevision == REV2 && current.audioPath != previous.audioPath);
        assert(LittleFS.files.at(current.audioPath)->bytes == audio);
        assert(LittleFS.files.at(previous.audioPath)->bytes == "old audio");
        assert(!LittleFS.exists(current.audioPath + ".tmp"));
        assert(LittleFS.totalBytes() - LittleFS.usedBytes() >= 8192);
        ScenarioSyncService restored(auth, "http://lab.example", "MAC");
        assert(restored.loadPersistedAssignments());
        assert(restored.getAssignment(1).audioPath == current.audioPath);
        // Cached revision needs no second audio request.
        httpReplies.push_back({MANIFEST, 200, manifest(REV2)});
        assert(sync.sync() && httpReplies.empty());
    }

    // Every failure must keep both the old assignment and its bytes.
    for (int failure = 0; failure < 13; ++failure) {
        resetFakes();
        ScenarioSyncService sync(auth, "https://lab.example", "MAC");
        seed(sync);
        const auto previous = sync.getAssignment(1);
        const auto nvs = Preferences::values["echoes_sync/manifest"];
        HttpReply audio{AUDIO, 200, std::string(4096, 'N')};
        switch (failure) {
        case 0: audio.length = 8192; audio.complete = false; break; // truncated length
        case 1: audio.chunked = true; audio.complete = false; break; // missing final chunk
        case 2: audio.readError = true; break; // timeout/reset
        case 3: audio.body.clear(); break; // empty
        case 4: audio.status = 403; break;
        case 5: audio.openError = true; break; // TLS/network error
        case 6: File::writeLimit = 17; break; // short flash write
        case 7: LittleFS.renameFails = true; break;
        case 8: File::flushError = true; break;
        case 9: audio.length = 2; break; // inconsistent length
        case 10: LittleFS.capacity = LittleFS.usedBytes() + 8192 + 4095; break;
        case 11:
            audio.chunked = true;
            LittleFS.capacity = LittleFS.usedBytes() + 8192 + 4095;
            break;
        case 12: Preferences::failWrite = true; break;
        }
        httpReplies.push_back({MANIFEST, 200, manifest(REV2)});
        httpReplies.push_back(audio);
        assert(!sync.sync());
        assert(sync.getAssignment(1).audioPath == previous.audioPath);
        assert(Preferences::values["echoes_sync/manifest"] == nvs);
        assert(LittleFS.files.at(previous.audioPath)->bytes == "old audio");
        for (const auto &entry : LittleFS.files) {
            assert(!String(entry.first).endsWith(".tmp"));
        }
        assert(liveHttpClients == 0 && httpReplies.empty());
    }

    resetFakes();
    ScenarioSyncService sync(auth, "http://lab.example", "MAC");
    seed(sync);
    const auto oldPath = sync.getAssignment(1).audioPath;
    for (int code : {401, 403, 500}) {
        httpReplies.push_back({MANIFEST, code, ""});
        assert(!sync.sync());
        assert(sync.getAssignment(1).audioPath == oldPath);
    }
    httpReplies.push_back({MANIFEST, 200, "{\"points\":[]}", -2, true, false});
    assert(!sync.sync()); // JSON that looks complete, but HTTP was incomplete
    assert(sync.getAssignment(1).audioPath == oldPath);
    httpReplies.push_back({MANIFEST, 200, "{\"points\":[{\"sensorIndex\":1,\"scenario\":null}]}"});
    assert(sync.sync() && !sync.getAssignment(1).has);
    httpReplies.push_back({MANIFEST, 200, "{\"points\":[]}"});
    assert(sync.sync());
    assert(LittleFS.exists(oldPath)); // unassignment does not delete an open file
}

void eventsApiTest(DeviceAuthenticator &auth) {
    resetFakes();
    ScenarioSyncService sync(auth, "https://lab.example/", "MAC");
    for (int status : {401, 403, 500, 202}) {
        httpReplies.push_back({"/events", status, ""});
        assert(sync.postEvent("CONNECTION") == (status == 202));
        JsonDocument doc;
        assert(deserializeJson(doc, httpCalls.back().body) == DeserializationError::Ok);
        assert(doc["type"] == "CONNECTION" && doc["scenarioId"].isNull());
        assert(httpCalls.back().method == HTTP_METHOD_POST);
        assert(httpCalls.back().headers.at("Authorization").rfind("Bearer ", 0) == 0);
    }
    for (const char *type : {"PLAY", "STOP"}) {
        httpReplies.push_back({"/events", 202, ""});
        assert(sync.postEvent(type, ID));
        JsonDocument doc;
        deserializeJson(doc, httpCalls.back().body);
        assert(doc["type"] == type && doc["scenarioId"] == ID);
    }
    assert(!sync.postEvent("PLAY"));
    assert(!sync.postEvent("STOP", "bad-uuid"));
    assert(!sync.postEvent("PAUSE", ID));
    assert(!sync.postEvent("CONNECTION", ID));
    assert(!sync.postEvent(nullptr));
    WiFi.state = 0;
    assert(!sync.postEvent("CONNECTION") && !sync.sync());
    assert(httpReplies.empty() && liveHttpClients == 0);
}

void authorizationTest(DeviceAuthenticator &auth) {
    resetFakes();
    ScenarioSyncService sync(auth, "https://lab.example/", "MAC");
    assert(!sync.loadPlaybackAuthorization() && !sync.isPlaybackAuthorized());
    httpReplies.push_back({"/playback-authorization", 500, ""});
    sync.refreshPlaybackAuthorization();
    assert(!sync.isPlaybackAuthorized());
    httpReplies.push_back({"/playback-authorization", 204, ""});
    sync.refreshPlaybackAuthorization();
    assert(sync.isPlaybackAuthorized() && Preferences::writes == 1);
    httpReplies.push_back({"/playback-authorization", 204, ""});
    sync.refreshPlaybackAuthorization();
    assert(Preferences::writes == 1); // unchanged decisions must not wear NVS every hour

    for (int status : {200, 302, 404, 429, 500, 503}) {
        httpReplies.push_back({"/playback-authorization", status, ""});
        sync.refreshPlaybackAuthorization();
        assert(sync.isPlaybackAuthorized());
    }
    HttpReply timeout{"/playback-authorization", 204, ""};
    timeout.openError = true;
    httpReplies.push_back(timeout);
    sync.refreshPlaybackAuthorization();
    assert(sync.isPlaybackAuthorized());
    WiFi.state = 0;
    sync.refreshPlaybackAuthorization();
    const auto requestCount = httpCalls.size();
    for (int i = 0; i < 100; ++i) assert(sync.isPlaybackAuthorized());
    assert(httpCalls.size() == requestCount);

    ScenarioSyncService restored(auth, "https://lab.example", "MAC");
    assert(restored.loadPlaybackAuthorization() && restored.isPlaybackAuthorized());
    ScenarioSyncService otherDevice(auth, "https://lab.example", "OTHER");
    ScenarioSyncService otherApi(auth, "https://other.example", "MAC");
    assert(!otherDevice.loadPlaybackAuthorization() && !otherDevice.isPlaybackAuthorized());
    assert(!otherApi.loadPlaybackAuthorization() && !otherApi.isPlaybackAuthorized());

    WiFi.state = WL_CONNECTED;
    for (int denied : {401, 403}) {
        httpReplies.push_back({"/playback-authorization", denied, ""});
        sync.refreshPlaybackAuthorization();
        assert(!sync.isPlaybackAuthorized());
        assert(restored.loadPlaybackAuthorization() && !restored.isPlaybackAuthorized());
        httpReplies.push_back({"/playback-authorization", 503, ""});
        sync.refreshPlaybackAuthorization();
        assert(!sync.isPlaybackAuthorized());
        httpReplies.push_back({"/playback-authorization", 204, ""});
        sync.refreshPlaybackAuthorization();
        assert(sync.isPlaybackAuthorized());

        httpReplies.push_back({MANIFEST, denied, ""});
        assert(!sync.sync() && !sync.isPlaybackAuthorized());
        assert(restored.loadPlaybackAuthorization() && !restored.isPlaybackAuthorized());
        httpReplies.push_back({"/playback-authorization", 204, ""});
        sync.refreshPlaybackAuthorization();
    }
    Preferences::failWrite = true;
    httpReplies.push_back({"/playback-authorization", 403, ""});
    sync.refreshPlaybackAuthorization();
    assert(!sync.isPlaybackAuthorized());
    assert(!restored.loadPlaybackAuthorization() && !restored.isPlaybackAuthorized());
    httpReplies.push_back({"/playback-authorization", 204, ""});
    sync.refreshPlaybackAuthorization();
    assert(sync.isPlaybackAuthorized());
    assert(!restored.loadPlaybackAuthorization() && !restored.isPlaybackAuthorized());
    Preferences::failWrite = false;
    httpReplies.push_back({"/playback-authorization", 204, ""});
    sync.refreshPlaybackAuthorization();
    assert(restored.loadPlaybackAuthorization() && restored.isPlaybackAuthorized());

    for (const char *invalid : {"not-json", "{}", "{\"allowed\":true}", "{\"allowed\":\"true\"}"}) {
        Preferences::values["echoes_play/authorization"] = invalid;
        assert(!restored.loadPlaybackAuthorization() && !restored.isPlaybackAuthorized());
    }
    assert(liveHttpClients == 0 && httpReplies.empty());
    std::cout << "PASS: offline permission persists by device/API; transient failures preserve it, denials revoke it\n";
}

void offlineControlTest(TestKeys &keys, DeviceAuthenticator &auth) {
    resetFakes();
    ScenarioSyncService sync(auth, "https://lab.example", "MAC");
    seed(sync);
    httpReplies.push_back({"/playback-authorization", 204, ""});
    sync.refreshPlaybackAuthorization();
    WiFi.state = 0;
    keys.loaded = false;
    clockNow = 1000;
    ScenarioSyncService rebooted(auth, "https://lab.example", "MAC");
    assert(rebooted.loadPersistedAssignments() && rebooted.loadPlaybackAuthorization());

    EspAudioPlayer first(1, 2, 3, 4), second(5);
    EspWifiAP ap;
    std::atomic<int> playing{0};
    std::vector<PlaybackEvent> events;
    first.onPlayback([&](const PlaybackEvent &event) {
        playing.store(event.type == PlaybackEvent::PLAY ? 1 : 0);
        events.push_back(event);
    });
    QueueHandle_t commands = xQueueCreate(4, sizeof(AudioCommand));
    ControlTaskContext control{&rebooted, &first, &second, &ap, &playing, commands};
    GuardTaskContext guard{&rebooted, &first, &second, &playing};
    struct Done {};
    const auto runControl = [&] {
        const uint32_t start = millis();
        onDelay = [&] { if (static_cast<uint32_t>(millis() - start) >= 100) throw Done{}; };
        try { controlTask(&control); } catch (const Done &) {}
        onDelay = {};
    };
    const auto runGuard = [&] {
        onDelay = [] { throw Done{}; };
        try { guardTask(&guard); } catch (const Done &) {}
        onDelay = {};
    };
    const auto calls = httpCalls.size();
    AudioCommand press{AudioCommand::PRESS_1};
    assert(xQueueSend(commands, &press, 0) == pdTRUE);
    runControl();
    assert(playing.load() == 1 && events.size() == 1);
    runGuard();
    assert(playing.load() == 1 && events.size() == 1 && httpCalls.size() == calls);
    assert(events.front().type == PlaybackEvent::PLAY && std::strcmp(events.front().scenarioId, ID) == 0);

    WiFi.state = WL_CONNECTED;
    keys.loaded = true;
    clockNow = 1795176000;
    httpReplies.push_back({"/playback-authorization", 403, ""});
    rebooted.refreshPlaybackAuthorization();
    runGuard();
    assert(playing.load() == 0 && events.size() == 2 && events.back().type == PlaybackEvent::STOP);
    runGuard();
    assert(events.size() == 2);
    assert(xQueueSend(commands, &press, 0) == pdTRUE);
    runControl();
    assert(playing.load() == 0 && events.size() == 2 && httpCalls.size() == calls + 1);
    vQueueDelete(commands);
    std::cout << "PASS: sensor and guard play offline after reboot without JWT/HTTP; explicit denial stops playback\n";
}

void playbackTest() {
    resetFakes();
    Scenario selected{};
    std::strcpy(selected.audio, "/audio/test.mp3");
    LittleFS.open(selected.audio, "w");
    PlaybackEventQueue queue;
    EspAudioPlayer player(1, 2, 3, 4), other(5);
    std::vector<PlaybackEvent> emitted;
    auto handler = [&](const PlaybackEvent &event) {
        if (event.type == PlaybackEvent::PLAY) assert(AudioGeneratorMP3::beginSucceeds);
        emitted.push_back(event);
        queue.enqueue(event);
    };
    player.onPlayback(handler);
    other.onPlayback(handler);
    assert(!player.stop() && !player.loop());
    AudioGeneratorMP3::beginSucceeds = false;
    assert(!player.play(selected, ID) && emitted.empty());
    AudioGeneratorMP3::beginSucceeds = true;
    char uuid[37];
    std::strcpy(uuid, ID);
    assert(player.play(selected, uuid));
    std::memset(uuid, 'x', 36); // assignment/caller storage may change during playback
    assert(!other.play(selected, ID));
    assert(!other.stop());
    assert(!player.loop());
    AudioGeneratorMP3::keepRunning = false;
    assert(player.loop());
    assert(!player.loop() && !player.stop()); // only one STOP
    assert(emitted.size() == 2);
    assert(emitted[0].type == PlaybackEvent::PLAY && emitted[1].type == PlaybackEvent::STOP);
    assert(std::strcmp(emitted[1].scenarioId, ID) == 0);
    PlaybackEvent event;
    assert(queue.peek(event) && event.type == PlaybackEvent::PLAY);
    assert(queue.peek(event) && event.type == PlaybackEvent::PLAY); // failed POST: no pop
    queue.accept();
    assert(queue.peek(event) && event.type == PlaybackEvent::STOP);
    queue.accept();
    assert(!queue.peek(event));
    AudioGeneratorMP3::keepRunning = true;
    assert(other.play(selected, REV2));
    assert(other.stop()); // explicit stop (AP/guard), same lifecycle
    assert(!other.stop());
    assert(emitted.size() == 4 && std::strcmp(emitted[3].scenarioId, REV2) == 0);

    PlaybackEventQueue full;
    PlaybackEvent play{}, stop{};
    std::strcpy(play.scenarioId, ID);
    stop = play;
    stop.type = PlaybackEvent::STOP;
    for (int i = 0; i < 8; ++i) {
        assert(full.enqueue(play) && full.enqueue(stop));
    }
    assert(!full.enqueue(play));
    full.accept(); // consumer frees space before the dropped play finishes
    assert(full.enqueue(stop)); // suppress orphan STOP even though space exists
    assert(!full.enqueue(play)); // only one slot: reserve STOP as well
    assert(full.enqueue(stop));
    full.accept();
    assert(full.enqueue(play) && full.enqueue(stop));
    int count = 0;
    while (full.peek(event)) {
        assert(event.type == (count % 2 ? PlaybackEvent::STOP : PlaybackEvent::PLAY));
        full.accept();
        ++count;
    }
    assert(count == 16);
}

int main() {
    TestKeys keys;
    DeviceAuthenticator auth(keys, "AA:bb:CC:01:02:03");
    jwtTest(keys, auth);
    preflightTest(keys, auth);
    downloadsTest(auth);
    eventsApiTest(auth);
    authorizationTest(auth);
    playbackTest();
    offlineControlTest(keys, auth);
    std::cout << "PASS: downloads, rollback, API and player/queue contracts\n";
}
