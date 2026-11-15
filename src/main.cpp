#include "Config.h"
#include "application/service/ConfigurationMode.hpp"
#include "application/service/DeviceRegister.hpp"
#include "application/service/ScanService.hpp"
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
#include "infra/storage/LittleFsStorage.hpp"
#include "infra/time/SntpClock.hpp"
#include "web/SetupController.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include <nvs_flash.h>

extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  initArduino();
  Serial.setRxBufferSize(1024); // room to paste a whole PEM over the console
  Serial.begin(115200);
  analogReadResolution(12);

  EspWifiAP wifiAP;
  EspWifiSTA espWifiSTA;
  WifiSTAProxy wifiSTA{espWifiSTA};
  WifiSTALogger staLogger;
  WifiAPLogger apLogger;
  SetupController setupController{wifiSTA};
  WifiIndicator wifiIndicator;
  ScanService scanService{wifiSTA};
  ConfigurationMode configurationMode{wifiAP, setupController};

  wifiSTA.setup();
  wifiAP.start();

  const std::string mac = EspMacAddress::get();
  LittleFsStorage storage;
  MbedtlsDeviceKeys deviceKeys{storage, "/private.pem"};
  EspMqttClient mqtt{MQTT_BROKER_URL, mac};
  DeviceAuthenticator authenticator{deviceKeys, mac};
  SntpClock sntpClock;
  DeviceRegister deviceRegister{deviceKeys, mqtt,    authenticator,
                                sntpClock,  wifiSTA, configurationMode,
                                mac};
  DeviceRegisterLogger deviceRegisterLogger;
  MqttLogger mqttLogger;
  FileConsole fileConsole{storage};

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed, formatting...");
    if (!LittleFS.begin(true)) {
      // Keep running: the AP and the setup API still work without the UI.
      Serial.println("LittleFS format failed! Web UI unavailable.");
    }
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

  // Audio
  EspPressSensor pressSensor1{FSR_1};
  EspPressSensor pressSensor2{FSR_2};

  EspAudioPlayer audio1{I2S_BCLK, I2S_LRC, I2S_DOUT, AMP_1};
  EspAudioPlayer audio2{AMP_2};

  struct AudioTaskContext {
    EspAudioPlayer *audio1;
    EspAudioPlayer *audio2;
  };

  static AudioTaskContext audioContext{&audio1, &audio2};

  xTaskCreate(
      [](void *param) {
        auto *context = static_cast<AudioTaskContext *>(param);

        while (true) {
          context->audio1->loop();
          context->audio2->loop();

          vTaskDelay(pdMS_TO_TICKS(1));
        }
      },
      "audio-loop", 8192, &audioContext, 2, nullptr);

  Scenario scenario1{
      {},
      "/audio/traqueal.mp3",
      {},
      0,
  };

  Scenario scenario2{
      {},
      "/audio/cardiaca.mp3",
      {},
      0,
  };

  pressSensor1.onPress([&]() {
    Serial.println("[FSR 1] PRESS");
    if (audio1.play(scenario1)) {
      Serial.println("Foi o audio 1");
    } else {
      Serial.println("Falha ao tocar audio 1");
    }
  });
  pressSensor1.onRelease([&]() {
    Serial.println("[FSR 1] RELEASE");
    audio1.stop();
  });
  pressSensor2.onPress([&]() {
    Serial.println("[FSR 2] PRESS");
    if (audio2.play(scenario2)) {
      Serial.println("Foi o audio 2");
    } else {
      Serial.println("Falha ao tocar audio 2");
    }
  });
  pressSensor2.onRelease([&]() {
    Serial.println("[FSR 2] RELEASE");
    audio2.stop();
  });

  while (true) {
    fileConsole.poll();
    delay(25);
  }
}
