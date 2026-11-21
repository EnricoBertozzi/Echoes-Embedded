#pragma once

#include <cstdint>

// POD: FreeRTOS queues copy the UUID, never a String or a borrowed pointer.
struct PlaybackEvent {
    enum Type : uint8_t { PLAY, STOP };
    Type type = PLAY;
    char scenarioId[37] = {};
};
