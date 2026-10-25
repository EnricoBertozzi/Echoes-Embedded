#pragma once

#include "domain/entity/AuscultationPoint.hpp"
#include "domain/entity/Scenario.hpp"

class AudioPlayer {
public:
  virtual ~AudioPlayer() = default;

  virtual void play(const Scenario &scenario) = 0;
  virtual void pause(const PointId pointId) = 0;
  virtual void stop(const PointId &pointId) = 0;
};
