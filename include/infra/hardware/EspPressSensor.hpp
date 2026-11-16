#pragma once

#include "application/hardware/PressSensor.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
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
  void setActiveChecker(std::function<bool()> checker) { activeChecker = checker; }

private:
  std::vector<OnPressFn> onPressFns;
  std::vector<OnReleaseFn> onReleaseFns;
  TaskHandle_t task{nullptr};
  const int adcPin;
  bool ready{true};
  std::function<bool()> activeChecker = []() { return true; };

  void processPress();
};
