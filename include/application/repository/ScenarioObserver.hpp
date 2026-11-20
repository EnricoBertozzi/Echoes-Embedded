
#pragma once

#include "domain/entity/Scenario.hpp"

class ScenarioObserver {
public:
    virtual ~ScenarioObserver() = default;

    virtual void onSave(
        const Scenario &scenario
    ) = 0;

    virtual void onUpdate(
        const Scenario &oldScenario,
        const Scenario &newScenario
    ) = 0;

    virtual void onRemove(
        const Scenario &scenario
    ) = 0;
};

