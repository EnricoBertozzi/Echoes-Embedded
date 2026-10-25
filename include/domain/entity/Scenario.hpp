#pragma once

#include <cstdint>
#include <ctime>

using ScenarioId = uint8_t[16];
using ScenarioAudio = char[32];

struct Scenario {
  ScenarioId id;
  ScenarioAudio audio;
  uint8_t pointId[16];
  time_t validUntil;
};
