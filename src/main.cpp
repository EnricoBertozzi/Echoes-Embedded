
#include "Config.h"

#include "application/repository/ScenarioRepository.hpp"
#include "application/service/ConfigurationMode.hpp"
#include "application/service/DeviceRegister.hpp"
#include "application/service/PlaybackEventQueue.hpp"
#include "application/service/ScenarioSyncService.hpp"
#include "application/service/ScanService.hpp"
#include "application/service/WifiService.hpp"

#include "domain/entity/Scenario.hpp"

#include "infra/console/FileConsole.hpp"
#include "infra/hardware/EspAudioPlayer.hpp"
#include "infra/hardware/EspMacAddress.hpp"
#include "infra/hardware/EspPressSensor.hpp"
#include "infra/hardware/EspWifiAP.hpp"
#include "infra/hardware/EspWifiSTA.hpp"
#include "infra/hardware/WifiIndicator.hpp"
#include "infra/hardware/WifiSTAProxy.hpp"

#include "infra/logging/DeviceRegisterLogger.hpp"
#include "infra/logging/MqttLogger.hpp"
#include "infra/logging/WifiAPLogger.hpp"
#include "infra/logging/WifiSTALogger.hpp"

#include "infra/mqtt/EspMqttClient.hpp"
#include "infra/security/DeviceAuthenticator.hpp"
#include "infra/security/MbedtlsDeviceKeys.hpp"

#include "infra/storage/EspScenarioStorage.hpp"
#include "infra/storage/EspWifiStorage.hpp"
#include "infra/storage/LittleFsStorage.hpp"

#include "infra/time/SntpClock.hpp"
#include "web/SetupController.hpp"

#include <Arduino.h>
#include <LittleFS.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace {

struct AudioCommand {
    enum Type : uint8_t {
        PRESS_1,
        RELEASE_1,
        PRESS_2,
        RELEASE_2
    };

    Type type;
};

struct AudioTaskContext {
    EspAudioPlayer *audio1;
    EspAudioPlayer *audio2;
    EspWifiAP *ap;
};

// A generation catches even disconnect/reconnect cycles while HTTP is blocked.
class ConnectionTracker : public WifiSTAObserver {
public:
    std::atomic<uint32_t> generation{0};
    void onConnect(const WifiScanDto &) override { ++generation; }
    void onDisconnect(const WifiScanDto &) override { ++generation; }
    void onError(const WifiScanDto &) override { ++generation; }
};

struct SyncTaskContext {
    ScenarioSyncService *service;
    WifiSTAProxy *wifi;
    ConnectionTracker *connection;
    PlaybackEventQueue *events;
};

struct ControlTaskContext {
    ScenarioSyncService *service;
    EspAudioPlayer *audio1;
    EspAudioPlayer *audio2;
    EspWifiAP *ap;
    std::atomic<int> *playingSensor;
    QueueHandle_t queue;
};

struct GuardTaskContext {
    ScenarioSyncService *service;
    EspAudioPlayer *audio1;
    EspAudioPlayer *audio2;
    std::atomic<int> *playingSensor;
};

void audioTask(void *param) {
    auto *ctx = static_cast<AudioTaskContext *>(param);

    while (true) {
        if (ctx->ap->countConnect() > 0) {
            ctx->audio1->stop();
            ctx->audio2->stop();

            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // O retorno true sinaliza que o decodificador terminou.
            // Libera o sensor para aceitar um NOVO acionamento.
            const bool finished1 = ctx->audio1->loop();
            const bool finished2 = ctx->audio2->loop();

            if (finished1) {
                Serial.println("[FSR 1] Decoder encerrado");
            }
            if (finished2) {
                Serial.println("[FSR 2] Decoder encerrado");
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

void syncTask(void *param) {
    auto *ctx = static_cast<SyncTaskContext *>(param);

    vTaskDelay(pdMS_TO_TICKS(15000));

    uint32_t generation = ctx->connection->generation.load();
    bool connectionAccepted = false;
    uint32_t nextConnectionAttempt = millis();
    uint32_t nextEventAttempt = millis();
    uint32_t nextSync = millis();
    const auto due = [](uint32_t deadline) {
        return static_cast<int32_t>(millis() - deadline) >= 0;
    };

    while (true) {
        const uint32_t current = ctx->connection->generation.load();
        if (current != generation) {
            generation = current;
            connectionAccepted = false;
            nextConnectionAttempt = nextEventAttempt = nextSync = millis();
        }
        if (ctx->wifi->isConnected()) {
            if (!connectionAccepted && due(nextConnectionAttempt)) {
                const bool accepted = ctx->service->postEvent("CONNECTION");
                // A 202 for an older link must not mark the current one sent.
                connectionAccepted = accepted && ctx->wifi->isConnected() &&
                    ctx->connection->generation.load() == generation;
                nextConnectionAttempt = millis() + 5000;
            }
            if (ctx->connection->generation.load() != generation) {
                continue;
            }

            PlaybackEvent event;
            if (connectionAccepted && due(nextEventAttempt) &&
                ctx->events->peek(event)) {
                const bool accepted = ctx->service->postEvent(
                    event.type == PlaybackEvent::PLAY ? "PLAY" : "STOP",
                    String(event.scenarioId));
                if (accepted) {
                    ctx->events->accept();
                }
                nextEventAttempt = millis() + (accepted ? 0 : 5000);
            }

            if (due(nextSync)) {
                Serial.println("[ScenarioSync] Starting synchronization");
                const bool ok = ctx->service->sync();
                Serial.println(ok
                    ? "[ScenarioSync] Success"
                    : "[ScenarioSync] Failed; cached audio retained");
                nextSync = millis() + 60000;
            }
        } else {
            connectionAccepted = false;
            nextConnectionAttempt = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void controlTask(void *param) {
    auto *ctx = static_cast<ControlTaskContext *>(param);

    AudioCommand command{};

    while (true) {
        if (xQueueReceive(
            ctx->queue,
            &command,
            pdMS_TO_TICKS(100)
        ) != pdTRUE) {
            continue;
        }

        const bool first =
            command.type == AudioCommand::PRESS_1 ||
            command.type == AudioCommand::RELEASE_1;

        const bool pressing =
            command.type == AudioCommand::PRESS_1 ||
            command.type == AudioCommand::PRESS_2;

        const int sensorIndex = first ? 1 : 2;

        EspAudioPlayer *player =
            first ? ctx->audio1 : ctx->audio2;

        if (!pressing) {
            // Soltar o FSR NAO interrompe a faixa.
            // A reproducao termina naturalmente em EspAudioPlayer::loop().
            continue;
        }

        if (ctx->ap->countConnect() > 0) {
            continue;
        }

        // Uma saída I2S compartilhada: somente um
        // dos FSR pode reproduzir por vez.
        if (ctx->playingSensor->load() != 0) {
            Serial.println(
                "[AUDIO] Another sensor is active"
            );
            continue;
        }

        SensorScenarioAssignment assignment =
            ctx->service->getAssignment(
                sensorIndex
            );

        if (!assignment.has ||
            !LittleFS.exists(assignment.audioPath)) {

            Serial.printf(
                "[FSR %d] No synchronized audio\n",
                sensorIndex
            );

            continue;
        }

        if (!ctx->service->authorizePlayback()) {
            Serial.printf(
                "[FSR %d] Playback not authorized\n",
                sensorIndex
            );
            continue;
        }
        if (ctx->ap->countConnect() > 0) {
            continue;
        }

        // O evento de pressao ja foi detectado e passou pela
        // autorizacao da API. Mesmo que o usuario solte o FSR
        // durante a requisicao, reproduzir UMA vez ate o fim.

        Scenario selected{};

        if (assignment.audioPath.length() >=
            sizeof(selected.audio)) {
            Serial.println(
                "[AUDIO] File path too long"
            );
            continue;
        }

        std::strncpy(
            selected.audio,
            assignment.audioPath.c_str(),
            sizeof(selected.audio) - 1
        );

        selected.audio[
            sizeof(selected.audio) - 1
        ] = '\0';

        if (player->play(selected, assignment.scenarioId.c_str())) {
            Serial.printf(
                "[FSR %d] Playing %s\n",
                sensorIndex,
                assignment.audioPath.c_str()
            );
        } else {
            Serial.printf(
                "[FSR %d] MP3 decoder failed or busy\n",
                sensorIndex
            );
        }
    }
}

void guardTask(void *param) {
    auto *ctx = static_cast<GuardTaskContext *>(param);

    while (true) {
        if (ctx->playingSensor->load() != 0) {
            const bool authorized =
                ctx->service->authorizePlayback();

            if (!authorized) {
                ctx->audio1->stop();
                ctx->audio2->stop();

                Serial.println(
                    "[PlaybackGuard] Playback blocked or connection lost"
                );
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // namespace

static void app_task(void *) {
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    initArduino();

    Serial.setRxBufferSize(1024);
    Serial.begin(115200);

    analogReadResolution(12);

    // Não formatar a LittleFS automaticamente.
    // Uma falha de montagem não deve apagar os MP3.
    const bool filesystemReady =
        LittleFS.begin(false);

    if (!filesystemReady) {
        Serial.println(
            "[LittleFS] Mount failed; existing data preserved"
        );
    }

    EspWifiAP wifiAP;
    EspWifiSTA espWifiSTA;
    WifiSTAProxy wifiSTA{espWifiSTA};

    EspWifiStorage wifiStorage;

    WifiService wifiService{
        wifiAP,
        wifiSTA,
        wifiStorage
    };

    WifiSTALogger staLogger;
    WifiAPLogger apLogger;

    SetupController setupController{
        wifiService
    };

    WifiIndicator wifiIndicator;
    ScanService scanService{wifiSTA};

    ConfigurationMode configurationMode{
        wifiAP,
        setupController
    };

    wifiSTA.setup();
    wifiAP.start();

    const std::string mac =
        EspMacAddress::get();

    LittleFsStorage storage;
    ScenarioRepository scenarioRepository;

    EspScenarioStorage scenarioStorage{
        scenarioRepository
    };

    MbedtlsDeviceKeys deviceKeys{
        storage,
        "/private.pem"
    };

    EspMqttClient mqtt{
        MQTT_BROKER_URL,
        mac
    };

    DeviceAuthenticator authenticator{
        deviceKeys,
        mac
    };

    SntpClock sntpClock;

    DeviceRegister deviceRegister{
        deviceKeys,
        mqtt,
        authenticator,
        sntpClock,
        wifiSTA,
        configurationMode,
        mac
    };

    ScenarioSyncService scenarioSync{
        authenticator,
        API_BASE_URL,
        mac.c_str()
    };

    DeviceRegisterLogger deviceRegisterLogger;
    MqttLogger mqttLogger;
    ConnectionTracker connectionTracker;

    FileConsole fileConsole{storage};

    if (filesystemReady) {
        scenarioSync.loadPersistedAssignments();
    }

    scenarioRepository.registerObserver(
        &scenarioStorage
    );

    {
        std::vector<Scenario> loadedScenarios;

        scenarioStorage.loadAll(loadedScenarios);
        scenarioRepository.loadAll(loadedScenarios);

        Serial.printf(
            "[ScenarioStorage] Loaded %u scenarios\n",
            static_cast<unsigned>(
                loadedScenarios.size()
            )
        );
    }

    wifiSTA.registerObserver(&staLogger);
    wifiSTA.registerObserver(&setupController);
    wifiSTA.registerObserver(&wifiIndicator);
    wifiSTA.registerObserver(&scanService);
    wifiSTA.registerObserver(&deviceRegister);
    wifiSTA.registerObserver(&connectionTracker);

    wifiAP.registerObserver(&apLogger);

    setupController.setup();
    setupController.start();

    configurationMode.registerObserver(
        &scanService
    );

    configurationMode.registerObserver(
        &deviceRegister
    );

    configurationMode.begin();
    scanService.begin();

    mqtt.registerObserver(&mqttLogger);

    deviceRegister.registerObserver(
        &deviceRegisterLogger
    );

    deviceRegister.registerObserver(
        &scanService
    );

    deviceRegister.begin();

    wifiService.start();

    EspPressSensor pressSensor1{FSR_1};
    EspPressSensor pressSensor2{FSR_2};

    pressSensor1.setActiveChecker(
        [&wifiAP]() {
            return wifiAP.countConnect() == 0;
        }
    );

    pressSensor2.setActiveChecker(
        [&wifiAP]() {
            return wifiAP.countConnect() == 0;
        }
    );

    EspAudioPlayer audio1{
        I2S_BCLK,
        I2S_LRC,
        I2S_DOUT,
        AMP_1
    };

    EspAudioPlayer audio2{AMP_2};

    static std::atomic<int> playingSensor{0};
    PlaybackEventQueue playbackEvents;
    if (!playbackEvents.ready()) {
        Serial.println("[Events] Could not create playback event queue");
    }
    const auto trackPlayback = [&](int sensorIndex, const PlaybackEvent &event) {
        // Runs under the player mutex, including decoder EOF, so STOP cannot race PLAY.
        playingSensor.store(event.type == PlaybackEvent::PLAY ? sensorIndex : 0);
        if (!playbackEvents.enqueue(event)) {
            Serial.println("[Events] Queue full; playback event pair dropped");
        }
    };
    audio1.onPlayback([&](const PlaybackEvent &event) {
        trackPlayback(1, event);
    });
    audio2.onPlayback([&](const PlaybackEvent &event) {
        trackPlayback(2, event);
    });

    static AudioTaskContext audioContext{
        &audio1,
        &audio2,
        &wifiAP
    };

    xTaskCreate(
        audioTask,
        "audio-loop",
        8192,
        &audioContext,
        2,
        nullptr
    );

    static SyncTaskContext syncContext{
        &scenarioSync,
        &wifiSTA,
        &connectionTracker,
        &playbackEvents
    };

    if (filesystemReady) {
        xTaskCreate(
            syncTask,
            "scenario-sync",
            12288,
            &syncContext,
            1,
            nullptr
        );
    }

    QueueHandle_t commandQueue = xQueueCreate(
        16,
        sizeof(AudioCommand)
    );

    if (commandQueue == nullptr) {
        Serial.println(
            "[AUDIO] Could not create command queue"
        );
    }

    static ControlTaskContext controlContext{
        &scenarioSync,
        &audio1,
        &audio2,
        &wifiAP,
        &playingSensor,
        commandQueue
    };

    if (commandQueue != nullptr) {
        xTaskCreate(
            controlTask,
            "audio-control",
            12288,
            &controlContext,
            1,
            nullptr
        );
    }

    pressSensor1.onPress([commandQueue]() {
        AudioCommand command{
            AudioCommand::PRESS_1
        };

        if (commandQueue != nullptr) {
            xQueueSend(commandQueue, &command, 0);
        }
    });

    pressSensor1.onRelease([commandQueue]() {
        AudioCommand command{
            AudioCommand::RELEASE_1
        };

        if (commandQueue != nullptr) {
            xQueueSend(commandQueue, &command, 0);
        }
    });

    pressSensor2.onPress([commandQueue]() {
        AudioCommand command{
            AudioCommand::PRESS_2
        };

        if (commandQueue != nullptr) {
            xQueueSend(commandQueue, &command, 0);
        }
    });

    pressSensor2.onRelease([commandQueue]() {
        AudioCommand command{
            AudioCommand::RELEASE_2
        };

        if (commandQueue != nullptr) {
            xQueueSend(commandQueue, &command, 0);
        }
    });

    static GuardTaskContext guardContext{
        &scenarioSync,
        &audio1,
        &audio2,
        &playingSensor
    };

    xTaskCreate(
        guardTask,
        "playback-guard",
        12288,
        &guardContext,
        1,
        nullptr
    );

    while (true) {
        fileConsole.poll();
        delay(25);
    }
}

extern "C" void app_main(void) {
    xTaskCreate(
        app_task,
        "app_task",
        32768,
        nullptr,
        5,
        nullptr
    );
}
