#pragma once

#include "domain/entity/Scenario.hpp"
class ScenarioObserver {
public:
  ~ScenarioObserver() = default;

  void onSave(const Scenario &scenario);
  void onUpdate(const Scenario &oldScenario, const Scenario &newScenario);
  void onRemove(const Scenario &scenario);
};
