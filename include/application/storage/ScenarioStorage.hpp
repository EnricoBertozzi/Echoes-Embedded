#pragma once

#include "domain/entity/Scenario.hpp"
#include <vector>

class ScenarioStorage {
public:
  virtual ~ScenarioStorage() = default;
  virtual bool saveAll(const std::vector<Scenario> &list) = 0;
  virtual bool loadAll(std::vector<Scenario> &list) = 0;
  virtual bool clear() = 0;
};
