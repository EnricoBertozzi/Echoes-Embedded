#pragma once

#include "Config.h"
#include <Arduino.h>

// HTTPS obrigatório em produção (S3). 1 = permite http:// (somente laboratório
// isolado). Defina em include/Config.h; o padrão é 0.
#ifndef API_ALLOW_PLAIN_HTTP
#define API_ALLOW_PLAIN_HTTP 0
#endif

// Nome do servidor para TLS (SNI, validação do certificado e header Host) quando
// API_BASE_URL usa o IP em vez do domínio, evitando a resolução de DNS.
// Ex.: API_BASE_URL=https://3.137.203.217 e API_TLS_SERVER_NAME=echoes.avraham.dev.br.
// Vazio = o host da URL é usado normalmente. O certificado continua validado.
#ifndef API_TLS_SERVER_NAME
#define API_TLS_SERVER_NAME ""
#endif

// Só https:// é aceito, a não ser que o HTTP puro tenha sido liberado.
inline bool isAllowedApiUrl(const String &url, bool allowPlainHttp = API_ALLOW_PLAIN_HTTP != 0) {
    return url.startsWith("https://") || (allowPlainHttp && url.startsWith("http://"));
}
