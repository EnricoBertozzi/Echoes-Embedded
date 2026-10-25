#pragma once

#include <cstdint>
#include <ctime>

using ScenarioId = uint8_t[16];

struct Scenario {
  ScenarioId id;
  char audio[32];
  uint8_t pointId[16];
  time_t validUntil;
};
