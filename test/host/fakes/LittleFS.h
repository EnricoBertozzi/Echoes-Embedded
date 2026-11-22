#pragma once

#include "Arduino.h"
#include <limits>
#include <map>
#include <memory>
#include <set>

struct FakeFileData { std::string bytes; };
struct File {
    std::shared_ptr<FakeFileData> data;
    bool directory = false;
    static inline size_t writeLimit = std::numeric_limits<size_t>::max();
    static inline bool flushError = false;
    explicit operator bool() const { return data || directory; }
    bool isDirectory() const { return directory; }
    size_t size() const { return data ? data->bytes.size() : 0; }
    size_t write(const uint8_t *bytes, size_t count) {
        count = std::min(count, writeLimit);
        data->bytes.append(reinterpret_cast<const char *>(bytes), count);
        return count;
    }
    void flush() {}
    int getWriteError() const { return flushError; }
    void close() { data.reset(); directory = false; }
};

struct FakeLittleFS {
    std::map<std::string, std::shared_ptr<FakeFileData>> files;
    std::set<std::string> dirs;
    size_t capacity = 1536 * 1024;
    bool renameFails = false;
    bool openFails = false;
    size_t totalBytes() const { return capacity; }
    size_t usedBytes() const {
        size_t size = 0;
        for (const auto &entry : files) size += entry.second->bytes.size();
        return size;
    }
    bool exists(const String &path) const { return files.count(path) || dirs.count(path); }
    bool mkdir(const String &path) { dirs.insert(path); return true; }
    File open(const String &path, const char *mode) {
        if (openFails) return {};
        if (dirs.count(path)) return {nullptr, true};
        if (*mode == 'w') files[path] = std::make_shared<FakeFileData>();
        auto it = files.find(path);
        return it == files.end() ? File{} : File{it->second, false};
    }
    bool remove(const String &path) { return files.erase(path) != 0; }
    bool rename(const String &from, const String &to) {
        if (renameFails || !files.count(from)) return false;
        files[to] = files.at(from);
        files.erase(from);
        return true;
    }
};
inline FakeLittleFS LittleFS;
