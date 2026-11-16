#include "application/repository/ScenarioRepository.hpp"

#include <algorithm>
#include <cstring>

std::optional<Scenario> ScenarioRepository::findById(const ScenarioId id) {
  auto it = std::find_if(scenarios.begin(), scenarios.end(),
                         [id](const Scenario &scenario) {
                           return std::memcmp(scenario.id, id, 16) == 0;
                         });

  if (it == scenarios.end()) {
    return std::nullopt;
  }

  return *it;
}

std::vector<Scenario> ScenarioRepository::findByPointId(const PointId pointId) {
  std::vector<Scenario> result;

  for (const auto &scenario : scenarios) {
    if (std::memcmp(scenario.pointId, pointId, 16) == 0) {
      result.push_back(scenario);
    }
  }

  return result;
}

void ScenarioRepository::save(const Scenario &scenario) {
  if (findById(scenario.id).has_value()) {
    return;
  }

  scenarios.push_back(scenario);

  for (auto *observer : observers) {
    if (observer) {
      observer->onSave(scenario);
    }
  }
}

void ScenarioRepository::update(const Scenario &scenario) {
  auto it = std::find_if(scenarios.begin(), scenarios.end(),
                         [&scenario](const Scenario &current) {
                           return std::memcmp(current.id, scenario.id, 16) == 0;
                         });

  if (it == scenarios.end()) {
    return;
  }

  Scenario oldScenario = *it;

  *it = scenario;

  for (auto *observer : observers) {
    if (observer) {
      observer->onUpdate(oldScenario, scenario);
    }
  }
}

void ScenarioRepository::remove(const ScenarioId scenarioId) {
  auto it = std::find_if(scenarios.begin(), scenarios.end(),
                         [scenarioId](const Scenario &scenario) {
                           return std::memcmp(scenario.id, scenarioId, 16) == 0;
                         });

  if (it == scenarios.end()) {
    return;
  }

  Scenario removedScenario = *it;

  scenarios.erase(it);

  for (auto *observer : observers) {
    if (observer) {
      observer->onRemove(removedScenario);
    }
  }
}

void ScenarioRepository::loadAll(const std::vector<Scenario> &list) {
  scenarios = list;
}

std::vector<Scenario> ScenarioRepository::all() const { return scenarios; }

void ScenarioRepository::registerObserver(ScenarioObserver *scenarioObserver) {
  if (!scenarioObserver) {
    return;
  }

  auto it = std::find(observers.begin(), observers.end(), scenarioObserver);

  if (it == observers.end()) {
    observers.push_back(scenarioObserver);
  }
}

void ScenarioRepository::unregisterObserver(
    ScenarioObserver *scenarioObserver) {
  observers.erase(
      std::remove(observers.begin(), observers.end(), scenarioObserver),
      observers.end());
}
