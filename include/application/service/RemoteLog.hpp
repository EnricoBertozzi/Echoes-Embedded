#pragma once

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>

// Logs remotos do dispositivo.
//
// Produtores (observers, tasks) chamam log(): copia para um anel de tamanho fixo
// sob um spinlock curto. Nunca aloca no heap, nunca bloqueia e nunca usa rede,
// então não atrasa o áudio, os sensores nem o download dos MP3.
// Quem envia à API é só a task de rede (ScenarioSyncService::flushLogs), entre
// as etapas de sincronização — nunca durante um download.
//
// REGRAS DE SEGURANÇA: nunca registre senha de Wi-Fi, token JWT, chave privada
// ou conteúdo de private.pem. SSID e códigos de motivo são permitidos.

enum class LogLevel : uint8_t { Debug = 0, Info, Warn, Error };

enum class LogCategory : uint8_t {
    Wifi = 0,  // STA/AP, conexão e portal de configuração
    Net,       // HTTP/TLS contra a API
    Mqtt,      // broker MQTT
    Register,  // cadastro/chaves do dispositivo
    Sync,      // cenários, autorização e downloads
    Audio,     // reprodução
    System     // boot, memória, armazenamento
};

constexpr size_t LOG_CODE_SIZE = 24;
constexpr size_t LOG_MSG_SIZE = 112;

struct LogEntry {
    uint32_t seq = 0;       // crescente por boot; (bootId, seq) é a chave de idempotência
    uint32_t uptimeMs = 0;  // millis() da primeira ocorrência
    uint32_t epoch = 0;     // UTC em segundos; 0 se o SNTP ainda não sincronizou
    uint16_t count = 1;     // ocorrências agrupadas (anti-flood)
    LogLevel level = LogLevel::Info;
    LogCategory category = LogCategory::System;
    char code[LOG_CODE_SIZE] = {};
    char message[LOG_MSG_SIZE] = {};
};

class RemoteLog {
public:
    static RemoteLog &instance();

    // Seguro em qualquer task. Descarta o evento mais antigo se o anel encher.
    void log(LogLevel level, LogCategory category, const char *code,
             const char *format, ...) __attribute__((format(printf, 5, 6)));

    // Copia até `max` eventos pendentes (do mais antigo) sem removê-los.
    size_t peekBatch(LogEntry *out, size_t max, uint32_t &dropped);

    // Remove os eventos com seq <= lastSeq depois que a API aceitou (202).
    void commit(uint32_t lastSeq, uint32_t droppedReported);

    // Remove tudo até lastSeq sem enviar (lote rejeitado de forma permanente).
    void discard(uint32_t lastSeq, uint32_t droppedReported) {
        commit(lastSeq, droppedReported);
    }

    size_t pending() const;
    const char *bootId() const { return bootId_; }

    static const char *levelName(LogLevel level);
    static const char *categoryName(LogCategory category);

private:
    RemoteLog();

    static constexpr size_t CAPACITY = 40;
    static constexpr uint32_t DEDUP_WINDOW_MS = 10000;

    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    LogEntry ring_[CAPACITY];
    size_t head_ = 0;
    size_t size_ = 0;
    uint32_t nextSeq_ = 1;
    uint32_t dropped_ = 0;
    char bootId_[9] = {};
};

#define REMOTE_LOG(level, category, code, ...)                          \
    RemoteLog::instance().log(LogLevel::level, LogCategory::category,   \
                              code, __VA_ARGS__)
