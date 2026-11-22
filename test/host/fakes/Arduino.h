#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

// Only hardware boundaries are faked; the services/player under test are the production translation units.
class String : public std::string {
    // Arduino String is not an STL range; keep ArduinoJson on its String adapter.
    using const_iterator = std::string::const_iterator;
public:
    using std::string::string;
    String(const std::string &value) : std::string(value) {}
    String(const char *value) : std::string(value ? value : "") {}
    String &operator=(const char *value) {
        assign(value ? value : "");
        return *this;
    }
    bool isEmpty() const { return empty(); }
    bool startsWith(const char *prefix) const { return rfind(prefix, 0) == 0; }
    bool endsWith(const char *suffix) const {
        const size_t n = std::strlen(suffix);
        return size() >= n && compare(size() - n, n, suffix) == 0;
    }
    void remove(size_t pos) { erase(pos); }
    bool concat(const char *data, size_t count) { append(data, count); return true; }
    bool concat(const char *data) { append(data); return true; }
    bool concat(char c) { push_back(c); return true; }
};

struct SerialFake {
    std::string output;
    void println(const char *message) { output += std::string(message) + "\n"; }
    template <typename... Args> void printf(const char *format, Args... args) {
        char buffer[1024];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        output += buffer;
    }
};
inline SerialFake Serial;
inline uint32_t fakeMillis = 0;
inline std::function<void()> onDelay;
inline uint32_t millis() { return fakeMillis; }
inline void delay(uint32_t ms) {
    fakeMillis += ms;
    if (onDelay) onDelay();
}
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
