#pragma once
#include <openssl/sha.h>
inline int mbedtls_sha256(const unsigned char *data, size_t size,
                         unsigned char *output, int) {
    return SHA256(data, size, output) ? 0 : -1;
}
