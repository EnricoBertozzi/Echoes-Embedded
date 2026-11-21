#pragma once
#include "Arduino.h"

class Preferences {
public:
    static inline String manifest;
    static inline bool failWrite = false;
    bool begin(const char *, bool = false) { return true; }
    void end() {}
    String getString(const char *, const char *) { return manifest; }
    size_t putString(const char *, const String &value) {
        if (failWrite) return 0;
        manifest = value;
        return value.length();
    }
};
