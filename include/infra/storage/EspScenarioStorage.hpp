#pragma once

#include "application/repository/ScenarioObserver.hpp"
#include "application/repository/ScenarioRepository.hpp"
#include "application/storage/ScenarioStorage.hpp"

class EspScenarioStorage : public ScenarioStorage, public ScenarioObserver {
public:
  explicit EspScenarioStorage(ScenarioRepository &repository)
      : repository(repository) {}

  bool saveAll(const std::vector<Scenario> &list) override;
  bool loadAll(std::vector<Scenario> &list) override;
  bool clear() override;

  // ScenarioObserver: persist after changes
  void onSave(const Scenario &scenario) override;
  void onUpdate(const Scenario &oldScenario,
                const Scenario &newScenario) override;
  void onRemove(const Scenario &scenario) override;

private:
  ScenarioRepository &repository;
};
