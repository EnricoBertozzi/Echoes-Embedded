#include "infra/hardware/EspWifiAP.hpp"
#include "infra/hardware/EspWifiSTA.hpp"
#include "infra/logging/WifiSTALogger.hpp"
#include "web/SetupController.hpp"
#include <Arduino.h>
#include <LittleFS.h>

EspWifiAP wifiAP;
EspWifiSTA wifiSTA;
WifiSTALogger staLogger;
SetupController setupController{wifiSTA};

extern "C" void app_main(void) {
  initArduino();

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

  setupController.setup();
  setupController.start();

  while (true) {
    delay(1000);
  }
}
