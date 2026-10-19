#pragma once

#include "domain/entity/Scenario.hpp"

class AudioPlayer {
public:
  ~AudioPlayer() = default;

  virtual void play(const Scenario &scenario) = 0;
  virtual void pause(const Scenario &scenario) = 0;
  virtual void stop(const Scenario &scenario) = 0;
};
