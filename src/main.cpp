#include "application/service/ScanService.hpp"
#include "infra/hardware/EspWifiAP.hpp"
#include "infra/hardware/EspWifiSTA.hpp"
#include "infra/hardware/WifiIndicator.hpp"
#include "infra/hardware/WifiSTAProxy.hpp"
#include "infra/logging/WifiSTALogger.hpp"
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
  Serial.begin(115200);

  EspWifiAP wifiAP;
  EspWifiSTA espWifiSTA;
  WifiSTAProxy wifiSTA{espWifiSTA};
  WifiSTALogger staLogger;
  SetupController setupController{wifiSTA};
  WifiIndicator wifiIndicator;
  ScanService scanService{wifiSTA};

  wifiSTA.setup();
  wifiAP.start();

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed, formatting...");
    if (!LittleFS.begin(true)) {
      Serial.println("LittleFS format failed! Web UI unavailable.");
    }
  }

  wifiSTA.registerObserver(&staLogger);
  wifiSTA.registerObserver(&setupController);
  wifiSTA.registerObserver(&wifiIndicator);
  wifiSTA.registerObserver(&scanService);
  wifiAP.registerObserver(&scanService);

  setupController.setup();
  setupController.start();

  scanService.begin();

  while (true) {
    delay(1000);
  }
}
