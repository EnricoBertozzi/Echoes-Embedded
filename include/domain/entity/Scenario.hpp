
#pragma once

#include "domain/entity/AuscultationPoint.hpp"

#include <cstdint>
#include <ctime>

using ScenarioId = uint8_t[16];

// Caminho: /audio/<scenarioId>_<revision>.mp3
using ScenarioAudio = char[128];

struct Scenario {
    ScenarioId id;
    ScenarioAudio audio;
    PointId pointId;
    time_t validUntil;
};

