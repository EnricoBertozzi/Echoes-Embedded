#include "infra/hardware/EspPressSensor.hpp"
#include "Config.h"
#include <Arduino.h>
#include <string>

EspPressSensor::EspPressSensor(int adcPin) : adcPin(adcPin) {
  pinMode(adcPin, INPUT);
  // Arduino 3 initializes the ADC channel on its first read, before pin attenuation.
  analogRead(adcPin);
  analogSetPinAttenuation(adcPin, ADC_11db);

  std::string taskName = "press-sensor-" + std::to_string(adcPin);

  xTaskCreate(
      [](void *param) {
        auto *press = static_cast<EspPressSensor *>(param);
        press->processPress();
      },
      taskName.c_str(), 4096, this, 1, &task);
}

EspPressSensor::~EspPressSensor() { vTaskDelete(task); }

void EspPressSensor::processPress() {
  while (true) {
    if (!activeChecker()) {
      ready = true;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    int pressValue = analogRead(adcPin);
    if (pressValue < RELEASE_THRESHOLD && !ready) {
      ready = true;
      for (auto &fn : onReleaseFns) {
        fn();
      }
    }

    if (pressValue > PRESS_THRESHOLD && ready) {
      ready = false;
      for (auto &fn : onPressFns) {
        fn();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
