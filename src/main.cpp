#include "application/service/DeviceRegister.hpp"
#include "application/service/ScanService.hpp"
#include "infra/console/FileConsole.hpp"
#include "infra/hardware/EspMacAddress.hpp"
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

  EspWifiAP wifiAP;
  EspWifiSTA espWifiSTA;
  WifiSTAProxy wifiSTA{espWifiSTA};
  WifiSTALogger staLogger;
  WifiAPLogger apLogger;
  SetupController setupController{wifiSTA};
  WifiIndicator wifiIndicator;
  ScanService scanService{wifiSTA};

  wifiSTA.setup();
  wifiAP.start();

  const std::string mac = EspMacAddress::get();
  LittleFsStorage storage;
  MbedtlsDeviceKeys deviceKeys{storage, "/private.pem"};
  EspMqttClient mqtt{MQTT_BROKER_URL, mac};
  DeviceAuthenticator authenticator{deviceKeys, mac};
  SntpClock sntpClock;
  DeviceRegister deviceRegister{deviceKeys, mqtt, authenticator,
                                        sntpClock, mac};
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
  wifiAP.registerObserver(&scanService);
  wifiAP.registerObserver(&deviceRegister);

  setupController.setup();
  setupController.start();

  scanService.begin();

  mqtt.registerObserver(&mqttLogger);
  deviceRegister.registerObserver(&deviceRegisterLogger);
  deviceRegister.registerObserver(&scanService);
  deviceRegister.begin();

  while (true) {
    fileConsole.poll();
    delay(25);
  }
}
