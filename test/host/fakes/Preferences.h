#pragma once
#include "Arduino.h"
#include <map>

class Preferences {
public:
    static inline std::map<std::string, String> values;
    static inline bool failWrite = false;
    static inline unsigned writes = 0;
    bool begin(const char *name, bool = false) { prefix = std::string(name) + "/"; return true; }
    void end() {}
    String getString(const char *key, const char *fallback) {
        const auto entry = values.find(prefix + key);
        return entry == values.end() ? String(fallback) : entry->second;
    }
    size_t putString(const char *key, const String &value) {
        ++writes;
        if (failWrite) return 0;
        values[prefix + key] = value;
        return value.length();
    }
    bool remove(const char *key) { return values.erase(prefix + key) != 0; }
private:
    std::string prefix;
};
