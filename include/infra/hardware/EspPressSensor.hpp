#pragma once

#include "application/hardware/PressSensor.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

class EspPressSensor : public PressSensor {
public:
  EspPressSensor(int adcPin);
  ~EspPressSensor() override;
  void onPress(OnPressFn onPressFn) override {
    onPressFns.push_back(onPressFn);
  }
  void onRelease(OnReleaseFn onReleaseFn) override {
    onReleaseFns.push_back(onReleaseFn);
  }

private:
  std::vector<OnPressFn> onPressFns;
  std::vector<OnReleaseFn> onReleaseFns;
  TaskHandle_t task;
  const int adcPin;
  bool ready{true};

  void processPress();
};
