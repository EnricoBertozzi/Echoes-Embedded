#pragma once

#include "domain/entity/AuscultationPoint.hpp"
#include <cstdint>
#include <ctime>

using ScenarioId = uint8_t[16];
using ScenarioAudio = char[32];

struct Scenario {
  ScenarioId id;
  ScenarioAudio audio;
  PointId pointId;
  time_t validUntil;
};
