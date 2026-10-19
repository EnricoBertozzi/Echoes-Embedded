#pragma once

#include <cstdint>
#include <ctime>

struct Scenario {
  uint8_t id[16];
  char audio[32];
  uint8_t pointId[16];
  time_t validUntil;
};
