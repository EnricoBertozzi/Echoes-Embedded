#pragma once

#include "Config.h"
#include <cstdint>

using PointId = uint8_t[16];

struct AuscultationPoint {
  PointId id;
  char position[32];
  uint8_t scenarios[SCENARIOS_LIMIT][16];
};
