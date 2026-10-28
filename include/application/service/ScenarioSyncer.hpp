#pragma once

#include "application/hardware/FileStorage.hpp"
#include "application/service/Authenticator.hpp"
#include "application/service/ScenarioService.hpp"

class ScenarioSyncer {
public:
  ScenarioSyncer(Authenticator &authenticator, ScenarioService &service,
                 FileStorage &storage)
      : authenticator(authenticator), scenarioService(service),
        storage(storage) {}
  void sync();

private:
  Authenticator &authenticator;
  ScenarioService &scenarioService;
  FileStorage &storage;
};
