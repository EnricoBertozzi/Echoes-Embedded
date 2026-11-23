#include "application/service/RemoteLog.hpp"

#include <esp_random.h>
#include <esp_timer.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

constexpr time_t MIN_VALID_EPOCH = 1700000000; // antes disso o SNTP não sincronizou

uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// vsnprintf pode cortar no meio de um caractere UTF-8; remove o pedaço quebrado.
void trimBrokenUtf8(char *text) {
    size_t length = std::strlen(text);
    size_t start = length;
    while (start > 0 && (static_cast<uint8_t>(text[start - 1]) & 0xC0) == 0x80) {
        --start;
    }
    if (start == 0) {
        return;
    }
    const uint8_t lead = static_cast<uint8_t>(text[start - 1]);
    size_t expected = 1;
    if ((lead & 0xE0) == 0xC0) expected = 2;
    else if ((lead & 0xF0) == 0xE0) expected = 3;
    else if ((lead & 0xF8) == 0xF0) expected = 4;
    if (expected > 1 && (length - (start - 1)) < expected) {
        text[start - 1] = '\0';
    }
}

} // namespace

RemoteLog &RemoteLog::instance() {
    static RemoteLog log;
    return log;
}

RemoteLog::RemoteLog() {
    // Identifica este boot; a API usa (bootId, seq) para ignorar reenvios.
    std::snprintf(bootId_, sizeof(bootId_), "%08x",
                  static_cast<unsigned>(esp_random()));
}

void RemoteLog::log(LogLevel level, LogCategory category, const char *code,
                    const char *format, ...) {
    LogEntry entry;
    entry.level = level;
    entry.category = category;
    entry.uptimeMs = nowMs();
    std::snprintf(entry.code, sizeof(entry.code), "%s", code ? code : "UNKNOWN");

    va_list args;
    va_start(args, format);
    std::vsnprintf(entry.message, sizeof(entry.message), format, args);
    va_end(args);
    trimBrokenUtf8(entry.message);

    time_t now = 0;
    time(&now);
    entry.epoch = now >= MIN_VALID_EPOCH ? static_cast<uint32_t>(now) : 0;

    portENTER_CRITICAL(&lock_);
    if (size_ > 0) {
        // Mesmo evento repetido em sequência (ex.: Wi-Fi oscilando): só conta.
        LogEntry &last = ring_[(head_ + size_ - 1) % CAPACITY];
        if (last.level == entry.level && last.category == entry.category &&
            last.count < UINT16_MAX &&
            entry.uptimeMs - last.uptimeMs < DEDUP_WINDOW_MS &&
            std::strcmp(last.code, entry.code) == 0 &&
            std::strcmp(last.message, entry.message) == 0) {
            ++last.count;
            portEXIT_CRITICAL(&lock_);
            return;
        }
    }
    if (size_ == CAPACITY) {
        head_ = (head_ + 1) % CAPACITY; // descarta o mais antigo
        --size_;
        ++dropped_;
    }
    entry.seq = nextSeq_++;
    ring_[(head_ + size_) % CAPACITY] = entry;
    ++size_;
    portEXIT_CRITICAL(&lock_);
}

size_t RemoteLog::peekBatch(LogEntry *out, size_t max, uint32_t &dropped) {
    portENTER_CRITICAL(&lock_);
    const size_t count = size_ < max ? size_ : max;
    for (size_t i = 0; i < count; ++i) {
        out[i] = ring_[(head_ + i) % CAPACITY];
    }
    dropped = dropped_;
    portEXIT_CRITICAL(&lock_);
    return count;
}

void RemoteLog::commit(uint32_t lastSeq, uint32_t droppedReported) {
    portENTER_CRITICAL(&lock_);
    while (size_ > 0 && ring_[head_].seq <= lastSeq) {
        head_ = (head_ + 1) % CAPACITY;
        --size_;
    }
    dropped_ = dropped_ > droppedReported ? dropped_ - droppedReported : 0;
    portEXIT_CRITICAL(&lock_);
}

size_t RemoteLog::pending() const {
    portENTER_CRITICAL(&lock_);
    const size_t size = size_;
    portEXIT_CRITICAL(&lock_);
    return size;
}

const char *RemoteLog::levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

const char *RemoteLog::categoryName(LogCategory category) {
    switch (category) {
    case LogCategory::Wifi: return "WIFI";
    case LogCategory::Net: return "NET";
    case LogCategory::Mqtt: return "MQTT";
    case LogCategory::Register: return "REGISTER";
    case LogCategory::Sync: return "SYNC";
    case LogCategory::Audio: return "AUDIO";
    case LogCategory::System: return "SYSTEM";
    }
    return "SYSTEM";
}
