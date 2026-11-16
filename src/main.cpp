#include "Config.h"
#include "application/repository/ScenarioRepository.hpp"
#include "application/service/ConfigurationMode.hpp"
#include "application/service/DeviceRegister.hpp"
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

  EspWifiAP wifiAP;
  EspWifiSTA espWifiSTA;
  WifiSTAProxy wifiSTA{espWifiSTA};
  EspWifiStorage wifiStorage;
  WifiService wifiService{wifiAP, wifiSTA, wifiStorage};
  WifiSTALogger staLogger;
  WifiAPLogger apLogger;
  SetupController setupController{wifiService};
  WifiIndicator wifiIndicator;
  ScanService scanService{wifiSTA};
  ConfigurationMode configurationMode{wifiAP, setupController};

  wifiSTA.setup();
  wifiAP.start();

  const std::string mac = EspMacAddress::get();
  LittleFsStorage storage;
  ScenarioRepository scenarioRepository;
  EspScenarioStorage scenarioStorage{scenarioRepository};
  MbedtlsDeviceKeys deviceKeys{storage, "/private.pem"};
  EspMqttClient mqtt{MQTT_BROKER_URL, mac};
  DeviceAuthenticator authenticator{deviceKeys, mac};
  SntpClock sntpClock;
  DeviceRegister deviceRegister{deviceKeys, mqtt, authenticator, sntpClock,
                                wifiSTA, configurationMode, mac};
  ScenarioSyncService scenarioSync{authenticator, API_BASE_URL, mac.c_str()};
  DeviceRegisterLogger deviceRegisterLogger;
  MqttLogger mqttLogger;
  FileConsole fileConsole{storage};

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed, formatting...");
    if (!LittleFS.begin(true)) {
      Serial.println("LittleFS format failed! Web UI unavailable.");
    }
  }

  scenarioRepository.registerObserver(&scenarioStorage);
  {
    std::vector<Scenario> loadedScenarios;
    scenarioStorage.loadAll(loadedScenarios);
    scenarioRepository.loadAll(loadedScenarios);
    Serial.printf("[ScenarioStorage] loaded %u scenarios\n",
                  (unsigned)loadedScenarios.size());
  }

  wifiSTA.registerObserver(&staLogger);
  wifiSTA.registerObserver(&setupController);
  wifiSTA.registerObserver(&wifiIndicator);
  wifiSTA.registerObserver(&scanService);
  wifiSTA.registerObserver(&deviceRegister);
  wifiAP.registerObserver(&apLogger);

  setupController.setup();
  setupController.start();

  configurationMode.registerObserver(&scanService);
  configurationMode.registerObserver(&deviceRegister);
  configurationMode.begin();

  scanService.begin();

  mqtt.registerObserver(&mqttLogger);
  deviceRegister.registerObserver(&deviceRegisterLogger);
  deviceRegister.registerObserver(&scanService);
  deviceRegister.begin();

  wifiService.start();

  EspPressSensor pressSensor1{FSR_1};
  EspPressSensor pressSensor2{FSR_2};
  pressSensor1.setActiveChecker([&wifiAP]() { return wifiAP.countConnect() == 0; });
  pressSensor2.setActiveChecker([&wifiAP]() { return wifiAP.countConnect() == 0; });

  EspAudioPlayer audio1{I2S_BCLK, I2S_LRC, I2S_DOUT, AMP_1};
  EspAudioPlayer audio2{AMP_2};

  struct AudioTaskContext {
    EspAudioPlayer *audio1;
    EspAudioPlayer *audio2;
    EspWifiAP *ap;
  };

  static AudioTaskContext audioContext{&audio1, &audio2, &wifiAP};

  xTaskCreate(
      [](void *param) {
        auto *context = static_cast<AudioTaskContext *>(param);
        while (true) {
          if (context->ap->countConnect() > 0) {
            context->audio1->stop();
            context->audio2->stop();
            vTaskDelay(pdMS_TO_TICKS(100));
          } else {
            context->audio1->loop();
            context->audio2->loop();
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        }
      },
      "audio-loop", 8192, &audioContext, 2, nullptr);

  struct SyncTaskCtx {
    ScenarioSyncService *svc;
    WifiSTAProxy *wifi;
  };
  static SyncTaskCtx syncCtx{&scenarioSync, &wifiSTA};
  xTaskCreate(
      [](void *param) {
        auto *ctx = static_cast<SyncTaskCtx *>(param);
        vTaskDelay(pdMS_TO_TICKS(15000));
        while (true) {
          if (ctx->wifi->isConnected()) {
            Serial.println("[ScenarioSync] syncing...");
            ctx->svc->sync();
          }
          vTaskDelay(pdMS_TO_TICKS(60000));
        }
      },
      "scenario-sync", 12288, &syncCtx, 1, nullptr);

  pressSensor1.onPress([&]() {
    Serial.println("[FSR 1] PRESS");
    if (wifiAP.countConnect() > 0) {
      Serial.println("[FSR 1] ignored: AP client connected");
      return;
    }
    auto assign = scenarioSync.getAssignment(1);
    if (!assign.has || !LittleFS.exists(assign.audioPath)) {
      Serial.println("[FSR 1] no assigned audio");
      return;
    }
    Scenario tmp{};
    strncpy(tmp.audio, assign.audioPath.c_str(), sizeof(tmp.audio) - 1);
    tmp.audio[sizeof(tmp.audio) - 1] = '\0';
    if (audio1.play(tmp)) {
      Serial.println("[FSR 1] playing");
      scenarioSync.postEvent("PLAY", assign.scenarioId);
    } else {
      Serial.println("[FSR 1] failed to play");
    }
  });
  pressSensor1.onRelease([&]() {
    Serial.println("[FSR 1] RELEASE");
    audio1.stop();
    auto assign = scenarioSync.getAssignment(1);
    if (assign.has) {
      scenarioSync.postEvent("STOP", assign.scenarioId);
    }
  });
  pressSensor2.onPress([&]() {
    Serial.println("[FSR 2] PRESS");
    if (wifiAP.countConnect() > 0) {
      Serial.println("[FSR 2] ignored: AP client connected");
      return;
    }
    auto assign = scenarioSync.getAssignment(2);
    if (!assign.has || !LittleFS.exists(assign.audioPath)) {
      Serial.println("[FSR 2] no assigned audio");
      return;
    }
    Scenario tmp{};
    strncpy(tmp.audio, assign.audioPath.c_str(), sizeof(tmp.audio) - 1);
    tmp.audio[sizeof(tmp.audio) - 1] = '\0';
    if (audio2.play(tmp)) {
      Serial.println("[FSR 2] playing");
      scenarioSync.postEvent("PLAY", assign.scenarioId);
    } else {
      Serial.println("[FSR 2] failed to play");
    }
  });
  pressSensor2.onRelease([&]() {
    Serial.println("[FSR 2] RELEASE");
    audio2.stop();
    auto assign = scenarioSync.getAssignment(2);
    if (assign.has) {
      scenarioSync.postEvent("STOP", assign.scenarioId);
    }
  });

  while (true) {
    fileConsole.poll();
    delay(25);
  }
}

extern "C" void app_main(void) {
  xTaskCreate(app_task, "app_task", 32768, nullptr, 5, nullptr);
}
