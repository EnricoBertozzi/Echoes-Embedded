#pragma once
#include "LittleFS.h"
struct AudioFileSourceLittleFS {
    explicit AudioFileSourceLittleFS(const char *path) : opened(LittleFS.exists(path)) {}
    bool isOpen() const { return opened; }
    void close() { opened = false; }
    bool opened;
};
