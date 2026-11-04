#include "infra/hardware/EspWifiAP.hpp"
#include "infra/hardware/EspWifiSTA.hpp"
#include "infra/hardware/WifiIndicator.hpp"
#include "infra/logging/WifiSTALogger.hpp"
#include "web/SetupController.hpp"
#include <Arduino.h>
#include <LittleFS.h>

extern "C" void app_main(void) {
  initArduino();

  EspWifiAP wifiAP;
  EspWifiSTA wifiSTA;
  WifiSTALogger staLogger;
  SetupController setupController{wifiSTA};
  WifiIndicator wifiIndicator;

  Serial.begin(115200);
  wifiSTA.setup();
  wifiAP.start();

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed!");
    while (true) {
      delay(1000);
    }
  }

  wifiSTA.registerObserver(&staLogger);
  wifiSTA.registerObserver(&setupController);
  wifiSTA.registerObserver(&wifiIndicator);

  setupController.setup();
  setupController.start();

  while (true) {
    delay(1000);
  }
}
