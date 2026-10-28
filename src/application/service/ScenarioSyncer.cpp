#include "application/service/ScenarioSyncer.hpp"

void ScenarioSyncer::sync() {
  auto scenarios = scenarioService.fetchAll();
  for (auto &s : scenarios) {
    auto audio = scenarioService.fetchAudio(s.id);
  }
}
