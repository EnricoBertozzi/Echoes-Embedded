// The runner inserts ConnectionTracker, SyncTaskContext and syncTask from src/main.cpp.
#include "application/hardware/WifiSTAObserver.hpp"
#include "application/service/PlaybackEventQueue.hpp"
#include <WiFi.h>
#include <atomic>
#include <cassert>
#include <iostream>
#include <vector>

struct WifiSTAProxy {
    bool isConnected() { return WiFi.status() == WL_CONNECTED; }
};
struct DeviceRegister {
    bool accepted = false;
    bool isRegistered() const { return accepted; }
};
struct ScenarioSyncService {
    std::vector<std::string> calls;
    int syncs = 0;
    std::vector<uint32_t> authorizationChecks;
    std::function<bool(const char *)> report;
    void refreshPlaybackAuthorization() { authorizationChecks.push_back(millis()); }
    bool postEvent(const char *type, const String &id = String()) {
        assert(id.empty() == (std::strcmp(type, "CONNECTION") == 0));
        calls.emplace_back(type);
        return report(type);
    }
    bool sync() { ++syncs; return true; }
};

#include "network-production.inc"

struct Finished {};
void eventOrderingTest() {
    ConnectionTracker connection;
    WifiSTAProxy wifi;
    PlaybackEventQueue events;
    ScenarioSyncService service;
    DeviceRegister registration;
    SyncTaskContext context{&service, &wifi, &connection, &events, &registration};
    PlaybackEvent event;
    std::strcpy(event.scenarioId, "12345678-1234-1234-1234-123456789abc");
    assert(events.enqueue(event));
    event.type = PlaybackEvent::STOP;
    assert(events.enqueue(event));
    int connections = 0, plays = 0, stops = 0;
    bool reconnected = false;
    service.report = [&](const char *type) {
        assert(registration.accepted);
        if (std::strcmp(type, "CONNECTION") == 0) {
            ++connections;
            if (connections == 1) return false;
            if (connections == 2) {
                // A 202 from an old connection must not suppress the new one.
                connection.onDisconnect({});
                connection.onConnect({});
            }
            return true;
        }
        assert(connections == 3);
        if (std::strcmp(type, "PLAY") == 0) return ++plays > 1;
        ++stops;
        assert(plays == 2);
        return true;
    };
    onDelay = [&] {
        assert(millis() < 60000); // detect stuck retries without sleeping
        if (!registration.accepted) {
            assert(service.calls.empty() && service.syncs == 0 && service.authorizationChecks.empty());
            if (millis() >= 16000) registration.accepted = true;
        }
        if (stops == 1 && !reconnected) {
            reconnected = true;
            // This entire cycle occurs between two task iterations.
            connection.onDisconnect({});
            connection.onConnect({});
        }
        if (connections == 4) throw Finished{};
    };
    try {
        syncTask(&context);
    } catch (const Finished &) {}
    onDelay = {};
    assert(!events.peek(event));
    assert((service.calls == std::vector<std::string>{
        "CONNECTION", "CONNECTION", "CONNECTION", "PLAY", "PLAY", "STOP", "CONNECTION"}));
    std::cout << "PASS: HTTP waits for enrollment; connection acceptance/generation and ordered retries\n";
}

void hourlyAuthorizationTest(uint32_t start) {
    fakeMillis = start;
    WiFi.state = WL_CONNECTED;
    ConnectionTracker connection;
    WifiSTAProxy wifi;
    PlaybackEventQueue events;
    ScenarioSyncService service;
    DeviceRegister registration;
    registration.accepted = true;
    service.report = [](const char *) { return true; };
    SyncTaskContext context{&service, &wifi, &connection, &events, &registration};
    int stage = 0;
    onDelay = [&] {
        const auto &checks = service.authorizationChecks;
        if (checks.empty()) return; // initial task delay before enrollment/HTTP
        switch (stage++) {
        case 0:
            fakeMillis = checks.front() + 3600000 - 100;
            break;
        case 1:
            assert(checks.size() == 1);
            break;
        case 2:
            assert(checks.size() == 2);
            assert(static_cast<uint32_t>(checks[1] - checks[0]) == 3600000);
            WiFi.state = 0;
            connection.onDisconnect({});
            break;
        case 3:
            assert(checks.size() == 2);
            fakeMillis += 2 * 3600000;
            WiFi.state = WL_CONNECTED;
            connection.onConnect({});
            break;
        case 4:
            assert(checks.size() == 3);
            throw Finished{};
        default: assert(false);
        }
    };
    try { syncTask(&context); } catch (const Finished &) {}
    onDelay = {};
}

int main() {
    eventOrderingTest();
    hourlyAuthorizationTest(0);
    hourlyAuthorizationTest(UINT32_MAX - 30000);
    std::cout << "PASS: authorization checks on connect/hourly/reconnect, including millis rollover\n";
}
