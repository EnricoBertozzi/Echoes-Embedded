#include "infra/hardware/EspPressSensor.hpp"
#include "Config.h"
#include <Arduino.h>

EspPressSensor::EspPressSensor(int adcPin) : adcPin(adcPin) {
  pinMode(adcPin, INPUT);
  analogSetPinAttenuation(adcPin, ADC_11db);

  std::string taskName = "press-sensor-" + std::to_string(adcPin);

  xTaskCreate(
      [](void *param) {
        auto *press = static_cast<EspPressSensor *>(param);
        press->processPress();
      },
      taskName.c_str(), 4096, this, 1, &task);
}

EspPressSensor::~EspPressSensor() { vTaskDelete(&task); }

void EspPressSensor::processPress() {
  while (true) {
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
