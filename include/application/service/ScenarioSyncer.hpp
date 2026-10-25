#pragma once

#include "application/service/Authenticator.hpp"

class ScenarioSyncer {
public:
  ScenarioSyncer(Authenticator &authenticator) : authenticator(authenticator) {}
  void sync();

private:
  Authenticator &authenticator;
};
