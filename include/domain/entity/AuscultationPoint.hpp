#pragma once

#include "Config.h"
#include <cstdint>

struct AuscultationPoint {
  uint8_t id[16];
  char position[32];
  uint8_t scenarios[SCENARIOS_LIMIT][16];
};
