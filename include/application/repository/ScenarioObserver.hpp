#pragma once

#include "domain/entity/Scenario.hpp"
class ScenarioObserver {
public:
  ~ScenarioObserver() = default;

  virtual void onSave(const Scenario &scenario);
  virtual void onUpdate(const Scenario &oldScenario,
                        const Scenario &newScenario);
  virtual void onRemove(const Scenario &scenario);
};
