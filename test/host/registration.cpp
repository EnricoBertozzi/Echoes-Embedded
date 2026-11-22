// The runner compiles DeviceRegister::run from production with deterministic task boundaries.
#include "application/service/DeviceRegisterObserver.hpp"
#include <freertos/FreeRTOS.h>
#include <atomic>
#include <cassert>
#include <iostream>
#include <vector>

constexpr uint32_t WAKE_INTERVAL_MS = 30000, MQTT_RECONNECT_INTERVAL_MS = 30000;
void ulTaskNotifyTake(int, uint32_t ticks) { delay(ticks); }

struct TestObserver : DeviceRegisterObserver {
    int waits = 0;
    void onWaitingForConfiguration() override { ++waits; }
};

class DeviceRegister {
public:
    enum class RegisterResult { Registered, Retry, Rejected };
    struct Wifi {
        bool connected = false;
        bool isConnected() const { return connected; }
    } wifiSTA;
    struct Configuration {
        bool active = false, associated = true;
        bool isActive() const { return active; }
        bool hasApClients() const { return associated; }
    } configurationMode;
    struct Clock {
        int syncs = 0;
        void sync() { ++syncs; }
    } timeSync;
    struct Keys {
        bool isLoaded() const { return true; }
    } keys;
    std::vector<DeviceRegisterObserver *> observers;
    std::atomic<bool> registered{false};
    int attempts = 0, tokens = 0;
    RegisterResult nextResult = RegisterResult::Retry;
    RegisterResult registerDevice() {
        assert(!registered.load());
        ++attempts;
        return nextResult;
    }
    void logAccessToken() { ++tokens; }
    void run();
};

#include "registration-production.inc"

struct Finished {};
int main() {
    DeviceRegister device;
    TestObserver observer;
    device.observers.push_back(&observer);
    int round = 0;
    onDelay = [&] {
        switch (++round) {
        case 1: break; // no Wi-Fi
        case 2:
            assert(device.attempts == 0);
            device.wifiSTA.connected = true;
            device.configurationMode.active = true;
            break;
        case 3:
            assert(observer.waits == 1 && device.attempts == 0);
            break;
        case 4:
            assert(observer.waits == 1 && device.attempts == 0);
            device.configurationMode.active = false;
            break; // the phone remains associated, but the portal is idle
        case 5:
            assert(device.attempts == 1 && !device.registered.load());
            device.nextResult = DeviceRegister::RegisterResult::Registered;
            break;
        case 6:
            assert(device.attempts == 2 && device.registered.load() && device.tokens == 1);
            break;
        case 7:
            assert(device.attempts == 2);
            device.configurationMode.active = true;
            break;
        case 8:
            assert(device.attempts == 2 && observer.waits == 2);
            device.configurationMode.active = false;
            device.nextResult = DeviceRegister::RegisterResult::Rejected;
            break;
        case 9:
            assert(device.attempts == 3 && !device.registered.load());
            break;
        case 10:
            assert(device.attempts == 3);
            device.wifiSTA.connected = false;
            break;
        case 11:
            device.wifiSTA.connected = true;
            device.nextResult = DeviceRegister::RegisterResult::Registered;
            break;
        case 12:
            assert(device.attempts == 4 && device.registered.load() && device.tokens == 2);
            assert(device.timeSync.syncs == 4 && device.configurationMode.associated);
            throw Finished{};
        default: assert(false);
        }
    };
    try {
        device.run();
    } catch (const Finished &) {}
    onDelay = {};
    std::cout << "PASS: active portal defers enrollment once; idle AP clients allow ACK/retries/reconnect\n";
}
