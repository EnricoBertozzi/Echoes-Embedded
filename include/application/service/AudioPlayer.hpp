#pragma once

#include "domain/entity/AuscultationPoint.hpp"
#include "domain/entity/Scenario.hpp"

class AudioPlayer {
public:
  virtual ~AudioPlayer() = default;

  virtual bool play(const Scenario &scenario) = 0;
  virtual bool stop() = 0;
};
