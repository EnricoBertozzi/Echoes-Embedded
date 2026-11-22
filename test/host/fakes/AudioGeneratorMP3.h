#pragma once
#include "AudioFileSourceLittleFS.h"
#include "AudioOutputI2S.h"
struct AudioGeneratorMP3 {
    static inline bool beginSucceeds = true;
    static inline bool keepRunning = true;
    bool running = false;
    bool begin(AudioFileSourceLittleFS *, AudioOutputI2S *) { return running = beginSucceeds; }
    bool loop() { return keepRunning; }
    bool isRunning() const { return running; }
    void stop() { running = false; }
};
