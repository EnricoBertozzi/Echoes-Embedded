#pragma once

#include "application/repository/ScenarioObserver.hpp"
#include "domain/entity/Scenario.hpp"
#include <optional>
#include <vector>
class ScenarioRepository {
public:
  std::optional<Scenario> findById(const ScenarioId id);
  std::vector<Scenario> findByPointId(const PointId pointId);
  void save(const Scenario &scenario);
  void update(const Scenario &scenario);
  void remove(const ScenarioId scenarioid);
  void registerObserver(ScenarioObserver *scenarioObserver);
  void unregisterObserver(ScenarioObserver *scenarioObserver);

private:
  std::vector<Scenario> scenarios;
  std::vector<ScenarioObserver *> observers;
};
