#pragma once

#include "Config.h"
#include <Arduino.h>

// HTTPS obrigatório em produção (S3). 1 = permite http:// (somente laboratório
// isolado). Defina em include/Config.h; o padrão é 0.
#ifndef API_ALLOW_PLAIN_HTTP
#define API_ALLOW_PLAIN_HTTP 0
#endif

// Só https:// é aceito, a não ser que o HTTP puro tenha sido liberado.
inline bool isAllowedApiUrl(const String &url, bool allowPlainHttp = API_ALLOW_PLAIN_HTTP != 0) {
    return url.startsWith("https://") || (allowPlainHttp && url.startsWith("http://"));
}
