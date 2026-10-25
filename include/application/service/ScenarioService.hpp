#pragma once

#include "application/service/dto/AudioDto.hpp"
#include "domain/entity/Scenario.hpp"
#include <vector>

class ScenarioService {
public:
  virtual ~ScenarioService() = default;

  virtual std::vector<Scenario> fetchAll();
  virtual std::vector<AudioDto> fetchAudio(ScenarioId scenarioId);
};
